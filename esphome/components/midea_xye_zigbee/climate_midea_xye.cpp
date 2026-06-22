#ifndef USE_ESP_IDF
#error "midea_xye_zigbee requires framework type esp-idf (ESP32-C6 / ESP32-H2)"
#endif

#include "climate_midea_xye.h"

#include "esphome/components/climate/climate_mode.h"
#include "esphome/core/log.h"
#include "xye_log.h"

#ifdef USE_ZIGBEE
#include "esphome/components/zigbee/zigbee_esp32.h"
#endif

namespace esphome {
namespace midea {
namespace xye {

#ifdef USE_ZIGBEE
// Global pointer used by the static Zigbee action handler callback.
static ClimateMideaXYE *g_midea_xye_instance = nullptr;  // NOLINT
#endif

const char *const Constants::TAG = "midea_xye";
const char *const Constants::FREEZE_PROTECTION = "Freeze Protection";
const char *const Constants::SILENT = "Silent";
const char *const Constants::TURBO = "Turbo";

static void set_sensor(Sensor *sensor, float value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->get_raw_state() != value))
    sensor->publish_state(value);
}

static void set_sensor_raw(Sensor *sensor, uint8_t value) { set_sensor(sensor, static_cast<float>(value)); }

#ifdef USE_TEXT_SENSOR
static void set_text_sensor(text_sensor::TextSensor *sensor, const std::string &value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->state != value))
    sensor->publish_state(value);
}

static const char *fan_speed_nibble_name(uint8_t speed) {
  switch (speed) {
    case 0x01:
      return "HIGH";
    case 0x02:
      return "MEDIUM";
    case 0x03:
      return "LOW";
    case 0x04:
      return "LOW";
    default:
      return speed == 0 ? "OFF" : "UNKNOWN";
  }
}

static std::string describe_operation_mode_byte(uint8_t raw) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u (%s)", raw, enum_to_string(static_cast<OperationMode>(raw)));
  return buf;
}

static std::string describe_fan_mode_byte(uint8_t raw) {
  char buf[48];
  const bool auto_fan = (raw & FAN_AUTO_FLAG) != 0;
  const uint8_t speed = raw & FAN_SPEED_MASK;
  if (raw == 0) {
    snprintf(buf, sizeof(buf), "%u (OFF)", raw);
  } else if (auto_fan && speed != 0) {
    snprintf(buf, sizeof(buf), "%u (AUTO+%s)", raw, fan_speed_nibble_name(speed));
  } else if (auto_fan) {
    snprintf(buf, sizeof(buf), "%u (AUTO)", raw);
  } else {
    snprintf(buf, sizeof(buf), "%u (%s)", raw, fan_speed_nibble_name(speed));
  }
  return buf;
}

template<typename EnumType>
static std::string describe_flag_bits_byte(uint8_t raw, const std::map<EnumType, const char *> &flag_map) {
  char buf[64];
  if (raw == 0) {
    snprintf(buf, sizeof(buf), "0 (NONE)");
    return buf;
  }
  std::string labels;
  uint8_t remaining = raw;
  for (const auto &entry : flag_map) {
    const uint8_t bit = static_cast<uint8_t>(entry.first);
    if (bit == 0 || (remaining & bit) != bit)
      continue;
    if (!labels.empty())
      labels += '+';
    labels += entry.second;
    remaining &= static_cast<uint8_t>(~bit);
  }
  if (labels.empty()) {
    snprintf(buf, sizeof(buf), "%u (UNKNOWN)", raw);
  } else if (remaining != 0) {
    char extra[12];
    snprintf(extra, sizeof(extra), "+0x%02X", remaining);
    labels += extra;
    snprintf(buf, sizeof(buf), "%u (%s)", raw, labels.c_str());
  } else {
    snprintf(buf, sizeof(buf), "%u (%s)", raw, labels.c_str());
  }
  return buf;
}

static std::string describe_capabilities_byte(uint8_t raw) {
  return describe_flag_bits_byte(raw, EnumTraits<Capabilities>::get_map());
}
#endif

static void set_number(number::Number *number, float value) {
  if (number != nullptr && (!number->has_state() || number->state != value))
    number->publish_state(value);
}

#ifdef USE_BINARY_SENSOR
static void set_binary_sensor(binary_sensor::BinarySensor *sens, bool value) {
  if (sens != nullptr && (!sens->has_state() || sens->state != value))
    sens->publish_state(value);
}
#endif

template<typename T> void update_property(T &property, const T &value, bool &flag) {
  if (property != value) {
    property = value;
    flag = true;
  }
}

static const char *control_state_name(ControlState state) {
  switch (state) {
    case ControlState::WAIT_DATA:
      return "WAIT_DATA";
    case ControlState::SEND_SET:
      return "SEND_SET";
    case ControlState::SEND_FOLLOWME:
      return "SEND_FOLLOWME";
    case ControlState::SEND_QUERY:
      return "SEND_QUERY";
    case ControlState::SEND_QUERY_EXTENDED:
      return "SEND_QUERY_EXTENDED";
    default:
      return "UNKNOWN";
  }
}

static void log_bus_state_transition_(const char *tag, ControlState from, ControlState to, const char *reason,
                                      uint8_t cmd = 0) {
  if (cmd != 0) {
    ESP_LOGD(tag, "Bus %s -> %s (%s, ack=0x%02X)", control_state_name(from), control_state_name(to), reason, cmd);
  } else {
    ESP_LOGD(tag, "Bus %s -> %s (%s)", control_state_name(from), control_state_name(to), reason);
  }
}

void ClimateMideaXYE::control(const ClimateCall &call) {
  if (call.get_mode().has_value()) {
    const ClimateMode new_mode = call.get_mode().value();
    ESP_LOGI(Constants::TAG, "HA mode -> %s", LOG_STR_ARG(climate::climate_mode_to_string(new_mode)));
    this->mode = new_mode;
    // Track HEAT_COOL intent separately so this->mode can always follow the bus.
    heat_cool_active_ = (new_mode == ClimateMode::CLIMATE_MODE_HEAT_COOL);
    // Reset Follow-Me initialization flag when mode changes to ensure
    // proper initialization sequence is sent on next Follow-Me update
    followMeInit = false;
    if (heat_cool_active_) {
      const float current = this->get_effective_current_temperature_();
      if (!std::isnan(current)) {
        const OperationMode prev = this->auto_bus_mode_;
        this->auto_bus_mode_ =
            XYEAdapter::resolve_auto_operation_mode(current, this->target_temperature, this->auto_bus_mode_);
        ESP_LOGD(Constants::TAG, "AUTO on mode change: room=%.1f°C target=%.1f°C bus %s -> %s", current,
                 this->target_temperature, enum_to_string(prev), enum_to_string(this->auto_bus_mode_));
      }
    }
  }
  if (call.get_target_temperature().has_value()) {
    const float new_target = call.get_target_temperature().value();
    ESP_LOGI(Constants::TAG, "HA target -> %.1f°C", new_target);
    this->target_temperature = new_target;
    if (heat_cool_active_) {
      const float current = this->get_effective_current_temperature_();
      if (!std::isnan(current)) {
        const OperationMode prev = this->auto_bus_mode_;
        this->auto_bus_mode_ =
            XYEAdapter::resolve_auto_operation_mode(current, this->target_temperature, this->auto_bus_mode_);
        ESP_LOGD(Constants::TAG, "AUTO on target change: room=%.1f°C target=%.1f°C bus %s -> %s", current,
                 this->target_temperature, enum_to_string(prev), enum_to_string(this->auto_bus_mode_));
      }
    }
  }
  if (call.get_fan_mode().has_value()) {
    ESP_LOGI(Constants::TAG, "HA fan -> %s", LOG_STR_ARG(climate::climate_fan_mode_to_string(call.get_fan_mode().value())));
    this->fan_mode = call.get_fan_mode().value();
  }
  if (call.get_swing_mode().has_value()) {
    ESP_LOGI(Constants::TAG, "HA swing -> %s",
             LOG_STR_ARG(climate::climate_swing_mode_to_string(call.get_swing_mode().value())));
    this->swing_mode = call.get_swing_mode().value();
  }
  if (call.get_preset().has_value()) {
    ESP_LOGI(Constants::TAG, "HA preset -> %s", LOG_STR_ARG(climate::climate_preset_to_string(call.get_preset().value())));
    this->preset = call.get_preset().value();
  }
  this->publish_state();
  this->request_set_();
#ifdef USE_ZIGBEE
  this->update_zigbee_thermostat_attrs_();
#endif
}

void ClimateMideaXYE::loop() {
#ifdef USE_ZIGBEE
  if (this->zb_req_.pending) {
    auto call          = this->make_call();
    const auto mode    = this->zb_req_.mode_req.load();
    const auto sp_raw  = this->zb_req_.setpoint_raw.load();
    const auto fan     = this->zb_req_.fan_req.load();

    // Clear atomics before performing so re-entrancy is safe.
    this->zb_req_.mode_req      = -1;
    this->zb_req_.setpoint_raw  = INT16_MIN;
    this->zb_req_.fan_req       = -1;
    this->zb_req_.pending       = false;

    if (mode != -1)
      call.set_mode(static_cast<climate::ClimateMode>(mode));
    if (sp_raw != INT16_MIN)
      call.set_target_temperature(static_cast<float>(sp_raw) / 100.0f);
    if (fan != -1)
      call.set_fan_mode(static_cast<climate::ClimateFanMode>(fan));

    call.perform();
  }
#endif
}

void ClimateMideaXYE::setup() {
  // this->uart_->check_uart_settings(4800, 1, UART_CONFIG_PARITY_NONE, 8);
  this->last_on_mode_ = *this->supported_modes_.begin();
  controlState = ControlState::SEND_QUERY;
  queuedCommand = ControlState::WAIT_DATA;
  ForceReadNextCycle = 1;
  followMeInit = false;

  // Register custom modes on the Climate base class. ESPHome 2026.4.0
  // deprecated the equivalent ClimateTraits setters in favor of these.
  this->set_supported_custom_presets(this->supported_custom_presets_);
  this->set_supported_custom_fan_modes(this->supported_custom_fan_modes_);

  // Start up in Auto fan mode (since unit doesn't report it correctly)
  this->fan_mode = ClimateFanMode::CLIMATE_FAN_AUTO;

#ifdef USE_ZIGBEE
  if (this->zigbee_) {
    // Our component runs AFTER ZigbeeComponent::setup() (lower setup priority
    // BEFORE_CONNECTION = -20 vs ZigbeeComponent default ≈ 300).  By the time
    // we get here, esp_zb_device_register() has already been called and the
    // Zigbee task is running.  Registering our handler last means it replaces
    // ESPHome's default handler (which only logs writes but takes no action).
    esp_zb_core_action_handler_register(zb_thermostat_action_handler_);
    ESP_LOGI(Constants::TAG, "Zigbee thermostat action handler registered (EP %u)", ZB_THERMO_EP);
  }
#endif
}

void ClimateMideaXYE::set_follow_me_sensor(Sensor *sensor) {
  this->follow_me_sensor_ = sensor;
  if (sensor != nullptr) {
    sensor->add_on_state_callback([this](float state) { this->on_follow_me_sensor_update_(state); });
  }
}

// TODO: Not sure if we really need this.
void ClimateMideaXYE::setPowerState(bool state) {
  if (state)
    this->mode = this->last_on_mode_;
  else
    this->mode = ClimateMode::CLIMATE_MODE_OFF;

  this->request_set_();
}

void ClimateMideaXYE::request_set_() {
  if (controlState == ControlState::WAIT_DATA) {
    ESP_LOGD(Constants::TAG, "Queue SET (queued was %s)", control_state_name(queuedCommand));
    queuedCommand = ControlState::SEND_SET;
  } else {
    const ControlState prev = controlState;
    controlState = ControlState::SEND_SET;
    log_bus_state_transition_(Constants::TAG, prev, controlState, "request SET");
  }
}

void ClimateMideaXYE::request_follow_me_() {
  if (controlState == ControlState::WAIT_DATA) {
    if (queuedCommand != ControlState::SEND_SET) {
      queuedCommand = ControlState::SEND_FOLLOWME;
    }
  } else if (controlState != ControlState::SEND_SET) {
    controlState = ControlState::SEND_FOLLOWME;
  }
}

void ClimateMideaXYE::advance_control_state_(uint8_t cmd_sent) {
  const ControlState prev = controlState;

  if (queuedCommand != ControlState::WAIT_DATA) {
    controlState = queuedCommand;
    queuedCommand = ControlState::WAIT_DATA;
    log_bus_state_transition_(Constants::TAG, prev, controlState, "queued command", cmd_sent);
    return;
  }

  switch (cmd_sent) {
    case CLIENT_COMMAND_QUERY:
      controlState = ControlState::SEND_QUERY_EXTENDED;
      break;
    case CLIENT_COMMAND_SET:
      controlState = ControlState::SEND_FOLLOWME;
      break;
    case CLIENT_COMMAND_QUERY_EXTENDED:
      controlState = ControlState::SEND_QUERY;
      break;
    case CLIENT_COMMAND_FOLLOWME:
      controlState = ControlState::SEND_QUERY;
      break;
    default:
      ESP_LOGW(Constants::TAG, "Unknown command %02X in state advance, resuming QUERY", cmd_sent);
      controlState = ControlState::SEND_QUERY;
      break;
  }
  log_bus_state_transition_(Constants::TAG, prev, controlState, "cycle", cmd_sent);
}

float ClimateMideaXYE::get_effective_current_temperature_() const {
  if (!std::isnan(this->current_temperature))
    return this->current_temperature;
  return this->internal_temperature_;
}

OperationMode ClimateMideaXYE::get_bus_operation_mode_() const {
  if (!heat_cool_active_)
    return XYEAdapter::get_operation_mode(this->mode);
  float current = this->get_effective_current_temperature_();
  if (std::isnan(current))
    current = this->target_temperature;
  return XYEAdapter::resolve_auto_operation_mode(current, this->target_temperature, this->auto_bus_mode_);
}

void ClimateMideaXYE::sync_auto_bus_mode_() {
  if (!heat_cool_active_ || post_set_grace_ > 0)
    return;
  const OperationMode desired = this->get_bus_operation_mode_();
  if (desired == this->auto_bus_mode_)
    return;
  const float current = this->get_effective_current_temperature_();
  ESP_LOGI(Constants::TAG, "AUTO bus switch %s -> %s (room=%.1f°C target=%.1f°C)", enum_to_string(this->auto_bus_mode_),
           enum_to_string(desired), current, this->target_temperature);
  this->auto_bus_mode_ = desired;
  this->request_set_();
}

void ClimateMideaXYE::setTransmitParams() {
  tx_data = TransmitData(Command::SET, this->server_id_, this->client_id_);
  auto &d = tx_data.message.data.standard;

  this->auto_bus_mode_ = this->get_bus_operation_mode_();
  d.operation_mode = this->auto_bus_mode_;

  if (!heat_cool_active_) {
    d.fan_mode = XYEAdapter::get_fan_mode(this->fan_mode.value());
  } else {
    // AUTO mode: let the unit decide fan speed.
    this->fan_mode = ClimateFanMode::CLIMATE_FAN_AUTO;
    d.fan_mode = FanMode::FAN_AUTO;
  }

  // Data always comes in as C, but user may want it set in F.
  d.target_temperature.value = XYEAdapter::get_raw_target_temperature(this->target_temperature, this->use_fahrenheit_);

  d.mode_flags = XYEAdapter::get_mode_flags(
      this->preset.value_or(ClimatePreset::CLIMATE_PRESET_NONE), this->swing_mode);

  tx_data.update_crc();
  ESP_LOGD(Constants::TAG, "SET build: bus_mode=%s (0x%02X) fan=%s target=%.1f°C raw=0x%02X flags=0x%02X",
           enum_to_string(d.operation_mode), static_cast<uint8_t>(d.operation_mode), enum_to_string(d.fan_mode),
           this->target_temperature, d.target_temperature.value, static_cast<uint8_t>(d.mode_flags));
}

void ClimateMideaXYE::sendRecv(uint8_t cmdSent) {
  // TODO: Reimplement flow control for manual RS485 flow control chips
  // digitalWrite(ComControlPin, RS485_TX_PIN_VALUE);
  log_frame_hex(Constants::TAG, ">>>", tx_data.raw, TX_MESSAGE_LENGTH);
  tx_data.print_debug(Constants::TAG, TX_MESSAGE_LENGTH, ESPHOME_LOG_LEVEL_DEBUG);
  this->uart_->write_array(tx_data.raw, TX_MESSAGE_LENGTH);
  this->uart_->flush();
  const ControlState prev = controlState;
  controlState = ControlState::WAIT_DATA;
  log_bus_state_transition_(Constants::TAG, prev, controlState, "await response", cmdSent);
  // Delay for response_timeout ms to allow response from the AC unit.
  this->set_timeout("read-result", this->response_timeout, [this, cmdSent]() {
    // digitalWrite(ComControlPin, RS485_RX_PIN_VALUE);

    uint8_t i = 0;
    while (this->uart_->available()) {
      if (i < RX_MESSAGE_LENGTH)
        this->uart_->read_byte(&rx_data.raw[i]);
      i++;
    }
    if (i == RX_MESSAGE_LENGTH) {
      log_frame_hex(Constants::TAG, "<<<", rx_data.raw, i);
      rx_data.print_debug(i, Constants::TAG, ESPHOME_LOG_LEVEL_DEBUG, this->use_fahrenheit_);
      // Don't parse responses to SET or FOLLOW_ME commands to avoid
      // overwriting the mode we just set. The AC state will be updated
      // on subsequent QUERY cycles.
      if (cmdSent != CLIENT_COMMAND_SET && cmdSent != CLIENT_COMMAND_FOLLOWME) {
        ParseResponse();
      } else {
        ESP_LOGD(Constants::TAG, "Skip parse for 0x%02X ack (SET/FOLLOW_ME)", cmdSent);
      }
      this->advance_control_state_(cmdSent);
    } else {
      ESP_LOGW(Constants::TAG, "Received incorrect message length %u from AC for Command %02X, resuming bus",
               i, cmdSent);
      if (i > 0)
        rx_data.print_debug(i, Constants::TAG, ESPHOME_LOG_LEVEL_WARN);
      this->advance_control_state_(cmdSent);
    }
  });
}

void ClimateMideaXYE::update() {
  uint8_t cmdSent = 0x00;
  // Possible States:
  // 0: Waiting for Response from Command
  // 1: Sending Set C3 Command
  // 2: Sending Set C6 Command
  // 3: Sending Query C0 Command
  // 4: Sending Query C4 Command
  switch (controlState) {
    case ControlState::SEND_SET: {
      setTransmitParams();
      post_set_grace_ = 2;
      cmdSent = CLIENT_COMMAND_SET;
      sendRecv(cmdSent);
      break;
    }
    case ControlState::SEND_FOLLOWME: {
      // If the AC mode changed, follow-me should be
      // refreshed, if emulating the wired controller's
      // behavior.
      cmdSent = CLIENT_COMMAND_FOLLOWME;
      sendRecv(cmdSent);
      if (this->mode == ClimateMode::CLIMATE_MODE_OFF) {
        ESP_LOGI(Constants::TAG, "Set static pressure.");
      } else {
        ESP_LOGI(Constants::TAG, "Sent Follow-Me data.");
      }
      break;
    }
    case ControlState::SEND_QUERY: {
      tx_data = TransmitData(Command::QUERY, this->server_id_, this->client_id_);
      tx_data.update_crc();
      cmdSent = CLIENT_COMMAND_QUERY;
      sendRecv(cmdSent);
      break;
    }
    case ControlState::SEND_QUERY_EXTENDED: {
      tx_data = TransmitData(Command::QUERY_EXTENDED, this->server_id_, this->client_id_);
      tx_data.update_crc();
      cmdSent = CLIENT_COMMAND_QUERY_EXTENDED;
      sendRecv(cmdSent);
      break;
    }
    case ControlState::WAIT_DATA: {
      // Wait for data to processed. Do nothing during the loop.
      break;
    }
  }
#ifdef USE_ZIGBEE
  this->update_zigbee_thermostat_attrs_();
#endif
}

void ClimateMideaXYE::ParseResponse() {
  if (!rx_data.is_valid()) {
    ESP_LOGE(Constants::TAG, "Received invalid response from AC");
    rx_data.print_debug(RX_MESSAGE_LENGTH, Constants::TAG, ESPHOME_LOG_LEVEL_ERROR);
    return;
  }

  switch (rx_data.message.frame.header.command) {
    case Command::QUERY: {
      const auto &qr = rx_data.message.data.query_response;
      ClimatePreset preset = ClimatePreset::CLIMATE_PRESET_NONE;

      const ClimateMode reported_mode = XYEAdapter::get_climate_mode(qr.operation_mode);
      // For action computation: when HEAT_COOL auto is active the thermostat decides
      // HEAT vs COOL, so pass HEAT_COOL to get_climate_action; otherwise use C0.
      const ClimateMode mode_for_action =
          heat_cool_active_ ? ClimateMode::CLIMATE_MODE_HEAT_COOL : reported_mode;

      if (static_cast<uint8_t>(qr.mode_flags) & MODE_FLAG_AUX_HEAT)
        preset = ClimatePreset::CLIMATE_PRESET_BOOST;
      else if (static_cast<uint8_t>(qr.mode_flags) & MODE_FLAG_ECO)
        preset = ClimatePreset::CLIMATE_PRESET_SLEEP;

      bool need_publish = false;

      if (post_set_grace_ > 0) {
        post_set_grace_--;
        ESP_LOGD(Constants::TAG,
                 "Post-SET grace: ignoring reported mode=%d (%u cycle(s) remaining)",
                 static_cast<int>(reported_mode), post_set_grace_);
      } else {
        // C0 is the source of truth for mode — always sync this->mode from the bus.
        // heat_cool_active_ is kept separately so AUTO thermostat switching still works
        // even though this->mode may now show COOL/HEAT/FAN instead of HEAT_COOL.
        // Clear heat_cool_active_ only when the unit reports OFF (user turned it off).
        if (reported_mode == ClimateMode::CLIMATE_MODE_OFF)
          heat_cool_active_ = false;
        update_property(this->mode, reported_mode, need_publish);
        if (reported_mode != ClimateMode::CLIMATE_MODE_OFF) {
          this->last_on_mode_ = reported_mode;
        }
      }

      if (reported_mode != ClimateMode::CLIMATE_MODE_OFF || this->mode != ClimateMode::CLIMATE_MODE_OFF ||
          ForceReadNextCycle == 1) {
        // Store the internal temperature from the XYE bus
        this->internal_temperature_ = XYEAdapter::get_temperature(qr.t1_temperature.value);

        // Publish the internal temperature to the sensor if configured
        set_sensor(this->internal_current_temperature_sensor_, this->internal_temperature_);

        // Update current_temperature based on sensor availability
        this->update_current_temperature_from_sensors_(need_publish);

        // C0 byte 10 is raw Celsius with SET_TEMP_STATUS_FLAG (0x40) masked — not the
        // (raw-0x28)/2 sensor encoding used by T1/T2/T3 (PROTOCOL.md Receive Messages).
        // C4 is preferred for Fahrenheit-capable units but many reply with 0xC5 garbage.
        // While HEAT_COOL (AUTO) is active the thermostat owns the setpoint; skip bus sync.
        {
          const float incoming_target_temp =
              XYEAdapter::get_target_temperature(qr.target_temperature.value, false);
          if (post_set_grace_ == 0 && !this->use_fahrenheit_ && !heat_cool_active_) {
            if (incoming_target_temp != this->target_temperature) {
              ESP_LOGD(Constants::TAG, "C0 setpoint sync: 0x%02X %.1f°C -> HA (was %.1f°C)",
                       qr.target_temperature.value, incoming_target_temp, this->target_temperature);
            }
            update_property(this->target_temperature, incoming_target_temp, need_publish);
          } else if (!this->use_fahrenheit_ && incoming_target_temp != this->target_temperature) {
            const char *reason = post_set_grace_ > 0 ? "post-SET grace"
                                 : heat_cool_active_          ? "HEAT_COOL owns setpoint"
                                                              : "fahrenheit/C4 path";
            ESP_LOGD(Constants::TAG, "C0 setpoint sync skipped (%s): bus 0x%02X %.1f°C HA %.1f°C", reason,
                     qr.target_temperature.value, incoming_target_temp, this->target_temperature);
          }
        }

        // Compressor/defrost-aware action is opt-in (compressor_aware_action) while the
        // C0 byte-19 compressor flag is still provisional. When disabled, compressor_active=true
        // and defrost_active=false reproduce the legacy "fan running implies heating/cooling".
        const bool compressor_active = !this->compressor_aware_action_ ||
                                       qr.compressor_running_flag == CompressorRunningFlag::ACTIVE;
        const bool defrost_active = this->compressor_aware_action_ &&
                                    XYEAdapter::is_defrost_active(qr.protect_flags.value());
        // While HEAT_COOL is selected, derive action from the thermostat sub-mode (HEAT/COOL
        // the ESP would send), not C0 operation_mode (wall sub-mode on a dual-master bus).
        this->sync_auto_bus_mode_();
        const OperationMode op_for_action = heat_cool_active_
                                              ? this->get_bus_operation_mode_()
                                              : qr.operation_mode;
        update_property(this->action,
                        XYEAdapter::get_climate_action(mode_for_action, qr.fan_mode, op_for_action,
                                                       compressor_active, defrost_active),
                        need_publish);

        if ((this->swing_mode != ClimateSwingMode::CLIMATE_SWING_OFF) !=
            (bool) (static_cast<uint8_t>(qr.mode_flags) & MODE_FLAG_SWING))
          need_publish = true;
        this->swing_mode = (static_cast<uint8_t>(qr.mode_flags) & MODE_FLAG_SWING)
                               ? ClimateSwingMode::CLIMATE_SWING_VERTICAL
                               : ClimateSwingMode::CLIMATE_SWING_OFF;
        if (this->preset != preset)
          need_publish = true;
        this->preset = preset;
      } else if ((this->action != climate::CLIMATE_ACTION_IDLE) &&
                 (static_cast<uint8_t>(qr.fan_mode) & FAN_SPEED_MASK) == 0x00) {
        this->action = climate::CLIMATE_ACTION_IDLE;
        need_publish = true;
      }

      // C0 fan mode sync: optional path for units that do not support C4 extended query.
      // Reads the physical running speed from C0 byte 9 and keeps HA fan_mode accurate:
      //   0x00 (fan stopped)  → skip; keep last commanded mode (avoids OFF-glitch on idle)
      //   bit 7 (0x80) set    → unit is auto-controlling speed → CLIMATE_FAN_AUTO
      //   bit 7 clear, >0     → unit reports explicit speed   → LOW / MEDIUM / HIGH
      // Skipped during post_set_grace_ to avoid overwriting a freshly-sent SET command.
      if (this->sync_fan_mode_from_c0_ && post_set_grace_ == 0) {
        const uint8_t fan_raw = static_cast<uint8_t>(qr.fan_mode);
        if (fan_raw != 0x00) {
          const ClimateFanMode c0_fan = XYEAdapter::get_climate_fan_mode(qr.fan_mode);
          if (!this->fan_mode.has_value() || this->fan_mode.value() != c0_fan) {
            ESP_LOGD(Constants::TAG, "C0 fan sync: 0x%02X -> %s", fan_raw,
                     LOG_STR_ARG(climate::climate_fan_mode_to_string(c0_fan)));
            this->fan_mode = c0_fan;
            need_publish = true;
          }
        }
      }

      if (need_publish)
        this->publish_state();

      set_sensor(this->temperature_2a_sensor_, XYEAdapter::get_temperature(qr.t2a_temperature.value));
      set_sensor(this->temperature_2b_sensor_, XYEAdapter::get_temperature(qr.t2b_temperature.value));
      set_sensor(this->temperature_3_sensor_, XYEAdapter::get_temperature(qr.t3_temperature.value));
      set_sensor(this->current_sensor_, static_cast<float>(qr.current));
      set_sensor(this->timer_start_sensor_, CalculateGetTime(qr.timer_start));
      set_sensor(this->timer_stop_sensor_, CalculateGetTime(qr.timer_stop));
      set_sensor(this->error_flags_sensor_, static_cast<float>(qr.error_flags.value()));
      set_sensor(this->protect_flags_sensor_, static_cast<float>(qr.protect_flags.value()));
      set_sensor(this->fan_speed_sensor_, static_cast<float>(XYEAdapter::get_fan_speed_level(qr.fan_mode)));
#ifdef USE_BINARY_SENSOR
      set_binary_sensor(this->defrost_sensor_, XYEAdapter::is_defrost_active(qr.protect_flags.value()));
      set_binary_sensor(this->compressor_active_sensor_,
                        qr.compressor_running_flag == CompressorRunningFlag::ACTIVE);
#endif

      // PROTOCOL.md C0 receive bytes 6-29 — publish all fields for bus tracing.
      set_sensor_raw(this->unknown1_sensor_, qr.unknown1);
      set_sensor_raw(this->capabilities_sensor_, static_cast<uint8_t>(qr.capabilities));
#ifdef USE_TEXT_SENSOR
      set_text_sensor(this->capabilities_text_sensor_,
                      describe_capabilities_byte(static_cast<uint8_t>(qr.capabilities)));
#endif
      set_sensor_raw(this->bus_operation_mode_sensor_, static_cast<uint8_t>(qr.operation_mode));
#ifdef USE_TEXT_SENSOR
      set_text_sensor(this->bus_operation_mode_text_sensor_,
                      describe_operation_mode_byte(static_cast<uint8_t>(qr.operation_mode)));
#endif
      set_sensor_raw(this->bus_fan_mode_sensor_, static_cast<uint8_t>(qr.fan_mode));
#ifdef USE_TEXT_SENSOR
      set_text_sensor(this->bus_fan_mode_text_sensor_, describe_fan_mode_byte(static_cast<uint8_t>(qr.fan_mode)));
#endif
      set_sensor(this->bus_target_temperature_sensor_,
                 XYEAdapter::get_target_temperature(qr.target_temperature.value, this->use_fahrenheit_));
      set_sensor_raw(this->unknown2_sensor_, qr.unknown2);
      set_sensor_raw(this->mode_flags_sensor_, static_cast<uint8_t>(qr.mode_flags));
      set_sensor_raw(this->operation_flags_sensor_, static_cast<uint8_t>(qr.operation_flags));
      set_sensor_raw(this->ccm_error_flags_sensor_, static_cast<uint8_t>(qr.ccm_communication_error_flags));
      set_sensor_raw(this->unknown4_sensor_, qr.unknown4);
      set_sensor_raw(this->unknown5_sensor_, qr.unknown5);
      set_sensor_raw(this->unknown6_sensor_, qr.unknown6);
      break;
    }
    case Command::QUERY_EXTENDED: {
      bool need_publish = false;
      const auto &exr = rx_data.message.data.extended_query_response;
      set_sensor(this->outdoor_sensor_, XYEAdapter::get_temperature(exr.outdoor_temperature.value));
      set_number(this->static_pressure_number_, static_cast<float>(STATIC_PRESSURE_VALUE_MASK & exr.static_pressure));
      // C4 is the sole source for target_temperature, covering both unit modes:
      //  - Fahrenheit: encoded as (°F + FAHRENHEIT_TEMP_OFFSET); convert to Celsius for ESPHome.
      //  - Celsius:    raw integer degrees with bit 6 (0x40) status flag; mask before use.
      // Respect post_set_grace_: skip until the C0 grace window has closed so a freshly-sent
      // SET command isn't immediately overwritten by stale device state.
      // Fahrenheit C4 decode approach adapted from rmounce/esphome@xye-units-switch.
      if ((this->mode != ClimateMode::CLIMATE_MODE_OFF || ForceReadNextCycle == 1) &&
          post_set_grace_ == 0) {
        const float incoming_target_temp =
            XYEAdapter::get_target_temperature(exr.target_temperature.value, this->use_fahrenheit_);
        update_property(this->target_temperature, incoming_target_temp, need_publish);
      }
      if (need_publish)
        this->publish_state();
      // Sync fan mode from the C4 target_fan_speed field when enabled. This is the commanded
      // speed as set on the physical thermostat and persists when the fan is idle, unlike
      // C0 fan_mode which reads 0x00 when stopped.
      // Respect post_set_grace_: if a SET was just issued the device may not yet have
      // updated its reported target_fan_speed, so skip until C0 has cleared the grace window.
      if (this->sync_fan_mode_from_device_ && post_set_grace_ == 0) {
        bool fan_need_publish = false;
        const ClimateFanMode new_fan_mode = XYEAdapter::get_climate_fan_mode(exr.target_fan_speed);
        if (!this->fan_mode.has_value() || this->fan_mode.value() != new_fan_mode) {
          this->fan_mode = new_fan_mode;
          fan_need_publish = true;
        }
        if (fan_need_publish)
          this->publish_state();
      }
      // Note: Previous versions validated fixed protocol marker bytes (0xBC, 0xD6, 0x80, 0x80, 0x80, 0x80)
      // but investigation shows these bytes are actually dynamic engineering values:
      // - Bytes 19-20 (0xBCD6): 16-bit compressor frequency or outdoor fan RPM
      // - Bytes 26-29 (0x80): Subsystem OK flags (compressor, outdoor fan, 4-way valve, inverter)
      // The validation has been removed to support all unit models correctly.

      // PROTOCOL.md C4 extended receive — publish all fields for bus tracing.
      set_sensor_raw(this->c4_indoor_fan_pwm_sensor_, static_cast<uint8_t>(exr.indoor_fan_pwm));
      set_sensor_raw(this->c4_indoor_fan_tach_sensor_, static_cast<uint8_t>(exr.indoor_fan_tach));
      set_sensor_raw(this->c4_compressor_flags_sensor_, static_cast<uint8_t>(exr.compressor_flags));
      set_sensor_raw(this->c4_esp_profile_sensor_, static_cast<uint8_t>(exr.esp_profile));
      set_sensor_raw(this->c4_protection_flags_sensor_, static_cast<uint8_t>(exr.protection_flags));
      set_sensor(this->c4_coil_inlet_sensor_, XYEAdapter::get_temperature(exr.coil_inlet_temp.value));
      set_sensor(this->c4_coil_outlet_sensor_, XYEAdapter::get_temperature(exr.coil_outlet_temp.value));
      set_sensor(this->c4_discharge_temp_sensor_, XYEAdapter::get_temperature(exr.discharge_temp.value));
      set_sensor_raw(this->c4_expansion_valve_sensor_, static_cast<uint8_t>(exr.expansion_valve_pos));
      set_sensor_raw(this->c4_system_status_flags_sensor_, static_cast<uint8_t>(exr.system_status_flags));
      set_sensor_raw(this->c4_target_fan_mode_sensor_, static_cast<uint8_t>(exr.target_fan_speed));
      set_sensor(this->c4_compressor_frequency_sensor_,
                 static_cast<float>(exr.compressor_freq_or_fan_rpm.value()));
      set_sensor_raw(this->c4_subsystem_compressor_sensor_, static_cast<uint8_t>(exr.subsystem_ok_compressor));
      set_sensor_raw(this->c4_subsystem_outdoor_fan_sensor_, static_cast<uint8_t>(exr.subsystem_ok_outdoor_fan));
      set_sensor_raw(this->c4_subsystem_4way_valve_sensor_, static_cast<uint8_t>(exr.subsystem_ok_4way_valve));
      set_sensor_raw(this->c4_subsystem_inverter_sensor_, static_cast<uint8_t>(exr.subsystem_ok_inverter));

      ForceReadNextCycle = 0;
      break;
    }
    default:
      break;
  }
}

uint8_t ClimateMideaXYE::CalculateSetTime(uint32_t time) {
  uint32_t current_time = time;
  uint8_t timeValue = 0;

  if (0 < (current_time / 960)) {
    timeValue |= 0x40;
    current_time = current_time % 960;
  }
  if (0 < (current_time / 480)) {
    timeValue |= 0x20;
    current_time = current_time % 480;
  }
  if (0 < (current_time / 240)) {
    timeValue |= 0x10;
    current_time = current_time % 240;
  }
  if (0 < (current_time / 120)) {
    timeValue |= 0x08;
    current_time = current_time % 120;
  }
  if (0 < (current_time / 60)) {
    timeValue |= 0x04;
    current_time = current_time % 60;
  }
  if (0 < (current_time / 30)) {
    timeValue |= 0x02;
    current_time = current_time % 30;
  }
  if (0 < (current_time / 15)) {
    timeValue |= 0x01;
    current_time = current_time % 15;
  }
  return timeValue;
}

uint32_t ClimateMideaXYE::CalculateGetTime(uint8_t time) {
  uint32_t timeValue = 0;

  if (time & 0x40) {
    timeValue += 960;
  }
  if (time & 0x20) {
    timeValue += 480;
  }
  if (time & 0x10) {
    timeValue += 240;
  }
  if (time & 0x08) {
    timeValue += 120;
  }
  if (time & 0x04) {
    timeValue += 60;
  }
  if (time & 0x02) {
    timeValue += 30;
  }
  if (time & 0x01) {
    timeValue += 15;
  }
  return timeValue;
}

climate::ClimateTraits ClimateMideaXYE::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_visual_min_temperature(17);
  traits.set_visual_max_temperature(30);
  traits.set_visual_temperature_step(1.0);
  traits.set_visual_current_temperature_step(VISUAL_CURRENT_TEMPERATURE_STEP);
  traits.set_supported_modes(this->supported_modes_);
  traits.set_supported_swing_modes(this->supported_swing_modes_);
  traits.set_supported_presets(this->supported_presets_);
  /* + MINIMAL SET OF CAPABILITIES */
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_AUTO);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_LOW);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_MEDIUM);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_HIGH);
  traits.add_supported_fan_mode(ClimateFanMode::CLIMATE_FAN_OFF);  // Can't set it but will be reported

  if (!traits.get_supported_modes().empty())
    traits.add_supported_mode(ClimateMode::CLIMATE_MODE_OFF);
  if (!traits.get_supported_swing_modes().empty())
    traits.add_supported_swing_mode(ClimateSwingMode::CLIMATE_SWING_OFF);
  if (!traits.get_supported_presets().empty())
    traits.add_supported_preset(ClimatePreset::CLIMATE_PRESET_NONE);

  return traits;
}

void ClimateMideaXYE::dump_config() {
  ESP_LOGCONFIG(Constants::TAG, "MideaXYE:");
  ESP_LOGCONFIG(Constants::TAG, "  [x] Period: %dms", this->get_update_interval());
  ESP_LOGCONFIG(Constants::TAG, "  [x] Response timeout: %dms", this->response_timeout);
  ESP_LOGCONFIG(Constants::TAG, "  [x] Use Fahrenheit: %d", this->use_fahrenheit_);

#ifdef USE_REMOTE_TRANSMITTER
  ESP_LOGCONFIG(Constants::TAG, "  [x] Using RemoteTransmitter");
#endif
  this->dump_traits_(Constants::TAG);
}

/* ACTIONS */

void ClimateMideaXYE::do_follow_me(float temperature, bool beeper) {
#ifdef USE_REMOTE_TRANSMITTER
  IrFollowMeData data(static_cast<uint8_t>(lroundf(temperature)), beeper);
  this->transmitter_.transmit(data);
#else
  // Prepare Follow-Me command for temperature update
  tx_data = TransmitData(Command::FOLLOW_ME, this->server_id_, this->client_id_);
  auto &d = tx_data.message.data.standard;

  // timer_stop is a subcommand type field for Follow-Me commands.
  // Subcommand values: 0x06=Init, 0x02=Update, 0x04=Static pressure
  // The followMeInit flag tracks whether we've sent the initialization command.
  // It gets reset to false whenever the AC mode changes (see control() function),
  // ensuring a proper initialization sequence after mode changes.
  d.timer_stop = followMeInit ? FOLLOWME_SUBCOMMAND_UPDATE : FOLLOWME_SUBCOMMAND_INIT;
  if (!followMeInit)
    followMeInit = true;
  lastFollowMeTemperature = static_cast<uint8_t>(lroundf(temperature));
  d.mode_flags = static_cast<ModeFlags>(lastFollowMeTemperature);
  tx_data.update_crc();
  // Only send if mode is something other than off.
  // Wired controller does not send Follow-Me command when off.
  if (this->mode != ClimateMode::CLIMATE_MODE_OFF) {
    this->request_follow_me_();
    ESP_LOGI(Constants::TAG, "Queued Follow-Me data.");
  }
#endif
}

void ClimateMideaXYE::set_static_pressure(uint8_t static_pressure) {
  if (static_pressure > 15) {
    ESP_LOGW(Constants::TAG, "Cannot set static pressure %d > 15", static_pressure);
    return;
  }

  // Prepare Follow-Me command for static pressure setting
  tx_data = TransmitData(Command::FOLLOW_ME, this->server_id_, this->client_id_);
  auto &d = tx_data.message.data.standard;
  d.target_temperature.value = static_cast<uint8_t>(STATIC_PRESSURE_FLAG | (static_pressure & STATIC_PRESSURE_VALUE_MASK));
  d.timer_stop = FOLLOWME_SUBCOMMAND_STOP;
  d.mode_flags = static_cast<ModeFlags>(lastFollowMeTemperature);
  tx_data.update_crc();

  if (this->mode == ClimateMode::CLIMATE_MODE_OFF) {
    this->request_follow_me_();
    ESP_LOGI(Constants::TAG, "Queued setting static pressure to %d", static_pressure);
  } else {
    ESP_LOGW(Constants::TAG, "Cannot set static pressure while unit is running");
  }
}

void ClimateMideaXYE::do_swing_step() {
#ifdef USE_REMOTE_TRANSMITTER
  IrSpecialData data(0x01);
  this->transmitter_.transmit(data);
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void ClimateMideaXYE::do_display_toggle() {
#ifdef USE_REMOTE_TRANSMITTER
  IrSpecialData data(0x08);
  this->transmitter_.transmit(data);
#else
  ESP_LOGW(Constants::TAG, "Action needs remote_transmitter component");
#endif
}

void ClimateMideaXYE::on_follow_me_sensor_update_(float state) {
  if (std::isnan(state)) {
    return;
  }
  
  // Update current_temperature with the sensor value
  bool need_publish = false;
  this->update_current_temperature_from_sensors_(need_publish);
  if (need_publish) {
    this->publish_state();
  }

  // Send follow_me command with the sensor temperature
  this->do_follow_me(state, false);
}

void ClimateMideaXYE::update_current_temperature_from_sensors_(bool &need_publish) {
  // Use follow_me_sensor as current_temperature if available, otherwise use internal temperature
  if (this->follow_me_sensor_ != nullptr && this->follow_me_sensor_->has_state() &&
      !std::isnan(this->follow_me_sensor_->state)) {
    update_property(this->current_temperature, this->follow_me_sensor_->state, need_publish);
  } else if (!std::isnan(this->internal_temperature_)) {
    update_property(this->current_temperature, this->internal_temperature_, need_publish);
  }
}

// ── Zigbee thermostat cluster implementation ──────────────────────────────
#ifdef USE_ZIGBEE

void ClimateMideaXYE::register_zigbee_thermostat(zigbee::ZigbeeComponent *zb) {
  this->zigbee_ = zb;
  g_midea_xye_instance = this;

  // ── Thermostat cluster (0x0201) ─────────────────────────────────────────
  // Use field-by-field assignment to avoid C designated-initializer ordering
  // issues: the SDK's struct field order varies between versions.
  esp_zb_thermostat_cluster_cfg_t tc{};
  tc.local_temperature             = static_cast<int16_t>(0x8000);  // invalid/unknown
  tc.occupied_cooling_setpoint     = 2600;   // 26.00 °C default
  tc.occupied_heating_setpoint     = 2000;   // 20.00 °C default
  tc.control_sequence_of_operation = 0x04;   // Cooling + Heating
  tc.system_mode                   = 0x03;   // Cool
  esp_zb_attribute_list_t *thermo_attrs = esp_zb_thermostat_cluster_create(&tc);

  // ── Fan Control cluster (0x0202) ────────────────────────────────────────
  esp_zb_fan_control_cluster_cfg_t fc{};
  fc.fan_mode          = 0x05;  // Auto
  fc.fan_mode_sequence = 0x04;  // Off/Low/Med/High/Auto
  esp_zb_attribute_list_t *fan_attrs = esp_zb_fan_control_cluster_create(&fc);

  // ── Cluster list: Basic + Identify (mandatory) + Thermostat + Fan ───────
  esp_zb_basic_cluster_cfg_t bc = {
      .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
      .power_source = 0x01,  // Mains (single phase)
  };
  esp_zb_cluster_list_t *clusters = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_basic_cluster(clusters, esp_zb_basic_cluster_create(&bc),
                                        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_identify_cluster(clusters, esp_zb_identify_cluster_create(nullptr),
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_thermostat_cluster(clusters, thermo_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_fan_control_cluster(clusters, fan_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  // ── Register endpoint directly into ZigbeeComponent's ep_list ───────────
  // create_endpoint() calls esp_zb_ep_list_add_ep() on the ep_list that
  // ZigbeeComponent::setup() will later pass to esp_zb_device_register().
  // This call happens before App.setup() so we are guaranteed to be early enough.
  // ESP_ZB_HA_THERMOSTAT_DEVICE_ID is esp_zb_ha_standard_devices_t; cast to the
  // ZBOSS zb_ha_standard_devs_e that ZigbeeComponent::create_endpoint() expects.
  // Both enums share the same underlying value (0x0301) for HA Thermostat.
  esp_err_t ret = zb->create_endpoint(ZB_THERMO_EP,
                                      static_cast<zb_ha_standard_devs_e>(ESP_ZB_HA_THERMOSTAT_DEVICE_ID),
                                      clusters);
  if (ret != ESP_OK) {
    ESP_LOGE(Constants::TAG, "Failed to create Zigbee thermostat endpoint: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(Constants::TAG, "Zigbee thermostat endpoint %u registered", ZB_THERMO_EP);
  }
}

void ClimateMideaXYE::update_zigbee_thermostat_attrs_() {
  if (!this->zigbee_ || !this->zigbee_->is_started())
    return;

  if (!esp_zb_lock_acquire(pdMS_TO_TICKS(10)))
    return;

  // local_temperature
  if (!std::isnan(this->current_temperature)) {
    auto local_temp = static_cast<int16_t>(this->current_temperature * 100.0f);
    esp_zb_zcl_set_attribute_val(ZB_THERMO_EP, ZB_CL_THERMOSTAT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ZB_ATTR_LOCAL_TEMP, &local_temp, false);
  }

  // cooling + heating setpoints (both track target_temperature)
  if (!std::isnan(this->target_temperature)) {
    auto sp = static_cast<int16_t>(this->target_temperature * 100.0f);
    esp_zb_zcl_set_attribute_val(ZB_THERMO_EP, ZB_CL_THERMOSTAT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ZB_ATTR_COOLING_SP, &sp, false);
    esp_zb_zcl_set_attribute_val(ZB_THERMO_EP, ZB_CL_THERMOSTAT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ZB_ATTR_HEATING_SP, &sp, false);
  }

  // system_mode
  auto sys_mode = climate_mode_to_zb(this->mode);
  esp_zb_zcl_set_attribute_val(ZB_THERMO_EP, ZB_CL_THERMOSTAT, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                               ZB_ATTR_SYSTEM_MODE, &sys_mode, false);

  // fan_mode
  if (this->fan_mode.has_value()) {
    auto fan = fan_mode_to_zb(this->fan_mode.value());
    esp_zb_zcl_set_attribute_val(ZB_THERMO_EP, ZB_CL_FAN_CONTROL, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                 ZB_ATTR_FAN_MODE, &fan, false);
  }

  esp_zb_lock_release();
}

// Static callback — invoked from the Zigbee FreeRTOS task.
// Sets atomic flags on ZbControlRequest and schedules loop() via
// enable_loop_soon_any_context() so the ESPHome main loop calls control().
esp_err_t ClimateMideaXYE::zb_thermostat_action_handler_(esp_zb_core_action_callback_id_t cb_id,
                                                          const void *msg) {
  // Forward non-attribute-write events to ESPHome's default behaviour (log only).
  if (cb_id != ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID)
    return ESP_OK;

  const auto *m = static_cast<const esp_zb_zcl_set_attr_value_message_t *>(msg);
  if (!m || m->info.status != ESP_ZB_ZCL_STATUS_SUCCESS)
    return ESP_ERR_INVALID_ARG;

  // Only handle writes directed at our thermostat endpoint.
  if (m->info.dst_endpoint != ZB_THERMO_EP)
    return ESP_OK;

  if (!g_midea_xye_instance)
    return ESP_OK;

  const uint16_t cluster = m->info.cluster;
  const uint16_t attr    = m->attribute.id;
  const void    *val     = m->attribute.data.value;

  if (cluster == ZB_CL_THERMOSTAT) {
    if (attr == ZB_ATTR_SYSTEM_MODE) {
      uint8_t zb_mode = *static_cast<const uint8_t *>(val);
      g_midea_xye_instance->zb_req_.mode_req = static_cast<int8_t>(zb_to_climate_mode(zb_mode));
      g_midea_xye_instance->zb_req_.pending  = true;
      ESP_LOGD(Constants::TAG, "Z2M system_mode write: 0x%02X -> ClimateMode %d", zb_mode,
               static_cast<int>(zb_to_climate_mode(zb_mode)));
    } else if (attr == ZB_ATTR_COOLING_SP || attr == ZB_ATTR_HEATING_SP) {
      int16_t sp_raw = *static_cast<const int16_t *>(val);
      g_midea_xye_instance->zb_req_.setpoint_raw = sp_raw;
      g_midea_xye_instance->zb_req_.pending      = true;
      ESP_LOGD(Constants::TAG, "Z2M setpoint write: %.2f °C", sp_raw / 100.0f);
    }
  } else if (cluster == ZB_CL_FAN_CONTROL) {
    if (attr == ZB_ATTR_FAN_MODE) {
      uint8_t zb_fan = *static_cast<const uint8_t *>(val);
      g_midea_xye_instance->zb_req_.fan_req  = static_cast<int8_t>(zb_to_fan_mode(zb_fan));
      g_midea_xye_instance->zb_req_.pending  = true;
      ESP_LOGD(Constants::TAG, "Z2M fan_mode write: 0x%02X -> ClimateFanMode %d", zb_fan,
               static_cast<int>(zb_to_fan_mode(zb_fan)));
    }
  }

  if (g_midea_xye_instance->zb_req_.pending)
    g_midea_xye_instance->enable_loop_soon_any_context();

  return ESP_OK;
}

#endif  // USE_ZIGBEE

}  // namespace xye
}  // namespace midea
}  // namespace esphome
