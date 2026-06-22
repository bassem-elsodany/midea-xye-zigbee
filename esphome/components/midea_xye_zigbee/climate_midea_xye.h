#pragma once


#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_traits.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#include "esphome/core/log.h"
#include "ir_transmitter.h"
#include "static_pressure_number.h"
#include "xye.h"
#include "xye_adapter.h"
#include "xye_send.h"
#include "xye_recv.h"
#ifdef USE_ZIGBEE
#include "zigbee_thermostat.h"
// Forward declaration — full definition pulled into climate_midea_xye.cpp via
// #include "esphome/components/zigbee/zigbee_esp32.h" (inside #ifdef USE_ZIGBEE).
// Declared here so the class members and method signatures compile without
// requiring the full header in every translation unit that includes this file.
namespace esphome {
namespace zigbee {
class ZigbeeComponent;
}  // namespace zigbee
}  // namespace esphome
#endif

namespace esphome {
namespace midea {
namespace xye {

// Bring XYE protocol types into this namespace
using xye::Command;
using xye::OperationMode;
using xye::FanMode;
using xye::FollowMeSubcommand;
using xye::ControlState;
using xye::TransmitData;
using xye::ReceiveData;

// Legacy compatibility - map old defines to new protocol definitions
// These provide backward compatibility for existing code using old names
constexpr uint8_t CLIENT_COMMAND_QUERY = static_cast<uint8_t>(Command::QUERY);
constexpr uint8_t CLIENT_COMMAND_QUERY_EXTENDED = static_cast<uint8_t>(Command::QUERY_EXTENDED);
constexpr uint8_t CLIENT_COMMAND_SET = static_cast<uint8_t>(Command::SET);
constexpr uint8_t CLIENT_COMMAND_FOLLOWME = static_cast<uint8_t>(Command::FOLLOW_ME);
constexpr uint8_t CLIENT_COMMAND_LOCK = static_cast<uint8_t>(Command::LOCK);
constexpr uint8_t CLIENT_COMMAND_UNLOCK = static_cast<uint8_t>(Command::UNLOCK);
constexpr uint8_t CLIENT_COMMAND_CELSIUS = static_cast<uint8_t>(Command::QUERY_EXTENDED);  // Legacy typo preserved for backward compatibility

constexpr uint8_t OP_MODE_OFF = static_cast<uint8_t>(OperationMode::OFF);
constexpr uint8_t OP_MODE_AUTO = static_cast<uint8_t>(OperationMode::AUTO);
constexpr uint8_t OP_MODE_FAN = static_cast<uint8_t>(OperationMode::FAN);
constexpr uint8_t OP_MODE_DRY = static_cast<uint8_t>(OperationMode::DRY);
constexpr uint8_t OP_MODE_HEAT = static_cast<uint8_t>(OperationMode::HEAT);
constexpr uint8_t OP_MODE_COOL = static_cast<uint8_t>(OperationMode::COOL);

constexpr uint8_t FAN_MODE_AUTO = static_cast<uint8_t>(FanMode::FAN_AUTO);
constexpr uint8_t FAN_MODE_OFF = static_cast<uint8_t>(FanMode::FAN_OFF);
constexpr uint8_t FAN_MODE_HIGH = static_cast<uint8_t>(FanMode::FAN_HIGH);
constexpr uint8_t FAN_MODE_MEDIUM = static_cast<uint8_t>(FanMode::FAN_MEDIUM);
constexpr uint8_t FAN_MODE_LOW = static_cast<uint8_t>(FanMode::FAN_LOW);
// FAN_AUTO_FLAG (0x80) and FAN_SPEED_MASK (0x0F) are defined in xye.h and available in this namespace.

constexpr uint8_t TEMP_SET_FAN_MODE = xye::TEMP_FAN_MODE;

constexpr uint8_t MODE_FLAG_AUX_HEAT = static_cast<uint8_t>(xye::ModeFlags::AUX_HEAT);
constexpr uint8_t MODE_FLAG_NORM = static_cast<uint8_t>(xye::ModeFlags::NORMAL);
constexpr uint8_t MODE_FLAG_ECO = static_cast<uint8_t>(xye::ModeFlags::ECO);
constexpr uint8_t MODE_FLAG_SWING = static_cast<uint8_t>(xye::ModeFlags::SWING);
constexpr uint8_t MODE_FLAG_VENT = static_cast<uint8_t>(xye::ModeFlags::VENTILATION);

constexpr uint8_t TIMER_15MIN = static_cast<uint8_t>(xye::TimerFlags::TIMER_15MIN);
constexpr uint8_t TIMER_30MIN = static_cast<uint8_t>(xye::TimerFlags::TIMER_30MIN);
constexpr uint8_t TIMER_1HOUR = static_cast<uint8_t>(xye::TimerFlags::TIMER_1HOUR);
constexpr uint8_t TIMER_2HOUR = static_cast<uint8_t>(xye::TimerFlags::TIMER_2HOUR);
constexpr uint8_t TIMER_4HOUR = static_cast<uint8_t>(xye::TimerFlags::TIMER_4HOUR);
constexpr uint8_t TIMER_8HOUR = static_cast<uint8_t>(xye::TimerFlags::TIMER_8HOUR);
constexpr uint8_t TIMER_16HOUR = static_cast<uint8_t>(xye::TimerFlags::TIMER_16HOUR);
constexpr uint8_t TIMER_INVALID = static_cast<uint8_t>(xye::TimerFlags::INVALID);

constexpr uint8_t FOLLOWME_SUBCOMMAND_UPDATE = static_cast<uint8_t>(FollowMeSubcommand::UPDATE);
constexpr uint8_t FOLLOWME_SUBCOMMAND_STOP = static_cast<uint8_t>(FollowMeSubcommand::STOP);
constexpr uint8_t FOLLOWME_SUBCOMMAND_INIT = static_cast<uint8_t>(FollowMeSubcommand::INIT);

// SERVER Response compatibility (same as client commands)
constexpr uint8_t SERVER_COMMAND_QUERY = static_cast<uint8_t>(Command::QUERY);
constexpr uint8_t SERVER_COMMAND_SET = static_cast<uint8_t>(Command::SET);
constexpr uint8_t SERVER_COMMAND_LOCK = static_cast<uint8_t>(Command::LOCK);
constexpr uint8_t SERVER_COMMAND_UNLOCK = static_cast<uint8_t>(Command::UNLOCK);

constexpr uint8_t CAPABILITIES_EXT_TEMP = static_cast<uint8_t>(xye::Capabilities::EXTERNAL_TEMP);
constexpr uint8_t CAPABILITIES_SWING = static_cast<uint8_t>(xye::Capabilities::SWING);

/// Precision step for `current_temperature` reported to Home Assistant (0.01 °C → 2 decimal places).
/// Applies to both the XYE-bus internal temperature and any external follow_me sensor value.
constexpr float VISUAL_CURRENT_TEMPERATURE_STEP = 0.01f;

constexpr uint8_t OP_FLAG_WATER_PUMP = static_cast<uint8_t>(xye::OperationFlags::WATER_PUMP);
constexpr uint8_t OP_FLAG_WATER_LOCK = static_cast<uint8_t>(xye::OperationFlags::WATER_LOCK);

using climate::ClimateAction;
using climate::ClimateCall;
using climate::ClimateFanMode;
using climate::ClimateMode;
using climate::ClimatePreset;
using climate::ClimateSwingMode;
using sensor::Sensor;

class Constants {
 public:
  static const char *const TAG;
  static const char *const FREEZE_PROTECTION;
  static const char *const SILENT;
  static const char *const TURBO;
};

#ifdef USE_ZIGBEE
// Forward declaration — the full definition lives in zigbee_esp32.h which pulls
// in heavy ESP Zigbee SDK headers; we avoid that in the shared .h file.
namespace esphome::zigbee { class ZigbeeComponent; }
#endif

class ClimateMideaXYE : public PollingComponent, public climate::Climate, public StaticPressureInterface {
 public:
  ClimateMideaXYE() : PollingComponent(1000), tx_data(Command::QUERY) { this->response_timeout = 100; }

#ifdef USE_REMOTE_TRANSMITTER
  void set_transmitter(RemoteTransmitterBase *transmitter) { this->transmitter_.set_transmitter(transmitter); }
#endif

  /* UART communication */

  void set_uart_parent(uart::UARTComponent *parent) { this->uart_ = parent; }
  void set_period(uint32_t ms) { this->set_update_interval(ms); }
  void set_response_timeout(uint32_t ms) { this->response_timeout = ms; }

  /* Component methods */

  float get_setup_priority() const override { return setup_priority::BEFORE_CONNECTION; }

  void dump_config() override;
  void set_outdoor_temperature_sensor(Sensor *sensor) { this->outdoor_sensor_ = sensor; }
  void set_fan_speed_sensor(Sensor *sensor) { this->fan_speed_sensor_ = sensor; }
  void set_temperature_2a_sensor(Sensor *sensor) { this->temperature_2a_sensor_ = sensor; }
  void set_temperature_2b_sensor(Sensor *sensor) { this->temperature_2b_sensor_ = sensor; }
  void set_temperature_3_sensor(Sensor *sensor) { this->temperature_3_sensor_ = sensor; }
  void set_current_sensor(Sensor *sensor) { this->current_sensor_ = sensor; }
  void set_timer_start_sensor(Sensor *sensor) { this->timer_start_sensor_ = sensor; }
  void set_timer_stop_sensor(Sensor *sensor) { this->timer_stop_sensor_ = sensor; }
  void set_error_flags_sensor(Sensor *sensor) { this->error_flags_sensor_ = sensor; }
  void set_protect_flags_sensor(Sensor *sensor) { this->protect_flags_sensor_ = sensor; }
  void set_humidity_setpoint_sensor(Sensor *sensor) { this->humidity_sensor_ = sensor; }
  void set_power_sensor(Sensor *sensor) { this->power_sensor_ = sensor; }
#ifdef USE_BINARY_SENSOR
  void set_defrost_sensor(binary_sensor::BinarySensor *sensor) { this->defrost_sensor_ = sensor; }
  void set_compressor_active_sensor(binary_sensor::BinarySensor *sensor) {
    this->compressor_active_sensor_ = sensor;
  }
#endif
  void set_follow_me_sensor(Sensor *sensor);
  void set_internal_current_temperature_sensor(Sensor *sensor) { this->internal_current_temperature_sensor_ = sensor; }
  void set_unknown1_sensor(Sensor *sensor) { this->unknown1_sensor_ = sensor; }
  void set_capabilities_sensor(Sensor *sensor) { this->capabilities_sensor_ = sensor; }
#ifdef USE_TEXT_SENSOR
  void set_capabilities_text_sensor(text_sensor::TextSensor *sensor) { this->capabilities_text_sensor_ = sensor; }
#endif
  void set_bus_operation_mode_sensor(Sensor *sensor) { this->bus_operation_mode_sensor_ = sensor; }
#ifdef USE_TEXT_SENSOR
  void set_bus_operation_mode_text_sensor(text_sensor::TextSensor *sensor) {
    this->bus_operation_mode_text_sensor_ = sensor;
  }
#endif
  void set_bus_fan_mode_sensor(Sensor *sensor) { this->bus_fan_mode_sensor_ = sensor; }
#ifdef USE_TEXT_SENSOR
  void set_bus_fan_mode_text_sensor(text_sensor::TextSensor *sensor) { this->bus_fan_mode_text_sensor_ = sensor; }
#endif
  void set_bus_target_temperature_sensor(Sensor *sensor) { this->bus_target_temperature_sensor_ = sensor; }
  void set_unknown2_sensor(Sensor *sensor) { this->unknown2_sensor_ = sensor; }
  void set_mode_flags_sensor(Sensor *sensor) { this->mode_flags_sensor_ = sensor; }
  void set_operation_flags_sensor(Sensor *sensor) { this->operation_flags_sensor_ = sensor; }
  void set_ccm_error_flags_sensor(Sensor *sensor) { this->ccm_error_flags_sensor_ = sensor; }
  void set_unknown4_sensor(Sensor *sensor) { this->unknown4_sensor_ = sensor; }
  void set_unknown5_sensor(Sensor *sensor) { this->unknown5_sensor_ = sensor; }
  void set_unknown6_sensor(Sensor *sensor) { this->unknown6_sensor_ = sensor; }
  void set_c4_indoor_fan_pwm_sensor(Sensor *sensor) { this->c4_indoor_fan_pwm_sensor_ = sensor; }
  void set_c4_indoor_fan_tach_sensor(Sensor *sensor) { this->c4_indoor_fan_tach_sensor_ = sensor; }
  void set_c4_compressor_flags_sensor(Sensor *sensor) { this->c4_compressor_flags_sensor_ = sensor; }
  void set_c4_esp_profile_sensor(Sensor *sensor) { this->c4_esp_profile_sensor_ = sensor; }
  void set_c4_protection_flags_sensor(Sensor *sensor) { this->c4_protection_flags_sensor_ = sensor; }
  void set_c4_coil_inlet_sensor(Sensor *sensor) { this->c4_coil_inlet_sensor_ = sensor; }
  void set_c4_coil_outlet_sensor(Sensor *sensor) { this->c4_coil_outlet_sensor_ = sensor; }
  void set_c4_discharge_temp_sensor(Sensor *sensor) { this->c4_discharge_temp_sensor_ = sensor; }
  void set_c4_expansion_valve_sensor(Sensor *sensor) { this->c4_expansion_valve_sensor_ = sensor; }
  void set_c4_system_status_flags_sensor(Sensor *sensor) { this->c4_system_status_flags_sensor_ = sensor; }
  void set_c4_target_fan_mode_sensor(Sensor *sensor) { this->c4_target_fan_mode_sensor_ = sensor; }
  void set_c4_compressor_frequency_sensor(Sensor *sensor) { this->c4_compressor_frequency_sensor_ = sensor; }
  void set_c4_subsystem_compressor_sensor(Sensor *sensor) { this->c4_subsystem_compressor_sensor_ = sensor; }
  void set_c4_subsystem_outdoor_fan_sensor(Sensor *sensor) { this->c4_subsystem_outdoor_fan_sensor_ = sensor; }
  void set_c4_subsystem_4way_valve_sensor(Sensor *sensor) { this->c4_subsystem_4way_valve_sensor_ = sensor; }
  void set_c4_subsystem_inverter_sensor(Sensor *sensor) { this->c4_subsystem_inverter_sensor_ = sensor; }
  void set_use_fahrenheit(bool yesno) { this->use_fahrenheit_ = yesno; }
  void set_compressor_aware_action(bool yesno) { this->compressor_aware_action_ = yesno; }
  /// Opt-in: when true, this->fan_mode is updated from C4 target_fan_speed on every extended
  /// query cycle, reflecting the fan setting on the physical thermostat in Home Assistant.
  void set_sync_fan_mode_from_device(bool yesno) { this->sync_fan_mode_from_device_ = yesno; }
  /// Opt-in: when true, this->fan_mode is updated from the C0 fan_mode byte on every query
  /// cycle. Respects the AUTO flag (0x80): AUTO flag set → CLIMATE_FAN_AUTO; no flag and fan
  /// running → maps to LOW/MEDIUM/HIGH; fan stopped (0x00) → no update (keeps last commanded).
  /// Useful when C4 extended query is unsupported (unit returns 0xC5).
  void set_sync_fan_mode_from_c0(bool yesno) { this->sync_fan_mode_from_c0_ = yesno; }
  /// Destination/indoor-unit address used in outgoing frames (0x00..0x3F, or 0xFF broadcast).
  void set_unit_id(NodeId id) { this->server_id_ = id; }
  /// Source/master address advertised by this controller in outgoing frames (0x00..0x3F).
  void set_master_id(NodeId id) { this->client_id_ = id; }
  void set_static_pressure_number(StaticPressureNumber *number) {
    this->static_pressure_number_ = number;
    number->set_parent(this);
  }
  void set_static_pressure(uint8_t value) override;
  void update() override;
  void setup() override;
  void loop() override;
  void sendRecv(uint8_t cmdSent);
  void setPowerState(bool state);
  void setTransmitParams();

  /* ############### */
  /* ### ACTIONS ### */
  /* ############### */

  void do_follow_me(float temperature, bool beeper = false);
  void do_display_toggle();
  void do_swing_step();
  // XYE RS-485 SET frames carry no dedicated beeper bit; beeper is only
  // signalled in FOLLOW_ME IR frames (see do_follow_me beeper param).
  void do_beeper_on()  {}
  void do_beeper_off() {}
  // TODO: Do we actually need these three?
  void do_power_on() { this->setPowerState(true); }
  void do_power_off() { this->setPowerState(false); }
  void do_power_toggle() { this->setPowerState(this->mode == ClimateMode::CLIMATE_MODE_OFF); }

  void set_supported_modes(climate::ClimateModeMask modes) { this->supported_modes_ = modes; }
  void set_supported_swing_modes(climate::ClimateSwingModeMask modes) { this->supported_swing_modes_ = modes; }
  void set_supported_presets(climate::ClimatePresetMask presets) { this->supported_presets_ = presets; }
  void set_custom_presets(std::vector<const char *> presets) { this->supported_custom_presets_ = presets; }
  void set_custom_fan_modes(std::vector<const char *> modes) { this->supported_custom_fan_modes_ = modes; }

  // Protocol message buffers - use unions for structured access
  TransmitData tx_data;
  ReceiveData rx_data;

#ifdef USE_ZIGBEE
  // Wire the ESPHome ZigbeeComponent for thermostat cluster support.
  // Called from Python-generated setup code (before App.setup()) to add the
  // HA Thermostat endpoint (0x0201 + 0x0202) to the Zigbee device registration.
  void register_zigbee_thermostat(zigbee::ZigbeeComponent *zb);
#endif

 private:
  ControlState controlState;
  uint8_t ForceReadNextCycle;
  ControlState queuedCommand;
  uint32_t response_timeout;
  // Tracks whether Follow-Me has been initialized after mode change.
  // When false, the next Follow-Me update sends an INIT subcommand (0x06).
  // When true, Follow-Me updates send a regular UPDATE subcommand (0x02).
  bool followMeInit;
  uint8_t lastFollowMeTemperature;
  // Number of upcoming QUERY responses whose reported mode should be
  // ignored after we issue a SET. Prevents a transient mode flap
  // (e.g. cool -> off -> cool) while the unit acknowledges the new mode.
  uint8_t post_set_grace_{0};

 protected:
  uart::UARTComponent *uart_;
#ifdef USE_REMOTE_TRANSMITTER
  IrTransmitter transmitter_;
#endif
  void control(const ClimateCall &call) override;
  climate::ClimateTraits traits() override;
  climate::ClimateModeMask supported_modes_{};
  climate::ClimateSwingModeMask supported_swing_modes_{};
  climate::ClimatePresetMask supported_presets_{};
  std::vector<const char *> supported_custom_presets_{};
  std::vector<const char *> supported_custom_fan_modes_{};
  bool use_fahrenheit_;
  // Opt-in (compressor_aware_action YAML option): when false, get_climate_action is
  // fed compressor_active=true / defrost_active=false to reproduce legacy behaviour.
  bool compressor_aware_action_{false};
  // Opt-in (sync_fan_mode_from_device YAML option): when true, fan_mode is updated from C4
  // target_fan_speed (the commanded speed from the physical thermostat).
  bool sync_fan_mode_from_device_{false};
  // Opt-in (sync_fan_mode_from_c0 YAML option): when true, fan_mode is updated from the C0
  // fan_mode byte so HA shows the physical running speed instead of the last commanded mode.
  bool sync_fan_mode_from_c0_{false};
  // True while the user has selected HEAT_COOL (AUTO) mode in HA.
  // Kept separately from this->mode so that this->mode can always be updated
  // from the C0 bus (source of truth) while the auto-switching logic still works.
  bool heat_cool_active_{false};
  // Outgoing frame addressing (unit_id / master_id YAML options). Defaults match the
  // protocol's typical single-unit values (SERVER_ID / CLIENT_ID = 0x00).
  NodeId server_id_{SERVER_ID};
  NodeId client_id_{CLIENT_ID};
  Sensor *outdoor_sensor_{nullptr};
  Sensor *fan_speed_sensor_{nullptr};
  Sensor *temperature_2a_sensor_{nullptr};
  Sensor *temperature_2b_sensor_{nullptr};
  Sensor *temperature_3_sensor_{nullptr};
  Sensor *current_sensor_{nullptr};
  Sensor *timer_start_sensor_{nullptr};
  Sensor *timer_stop_sensor_{nullptr};
  Sensor *error_flags_sensor_{nullptr};
  Sensor *protect_flags_sensor_{nullptr};
  Sensor *humidity_sensor_{nullptr};
  Sensor *power_sensor_{nullptr};
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *defrost_sensor_{nullptr};
  binary_sensor::BinarySensor *compressor_active_sensor_{nullptr};
#endif
  Sensor *follow_me_sensor_{nullptr};
  Sensor *internal_current_temperature_sensor_{nullptr};
  Sensor *unknown1_sensor_{nullptr};
  Sensor *capabilities_sensor_{nullptr};
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *capabilities_text_sensor_{nullptr};
#endif
  Sensor *bus_operation_mode_sensor_{nullptr};
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *bus_operation_mode_text_sensor_{nullptr};
#endif
  Sensor *bus_fan_mode_sensor_{nullptr};
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *bus_fan_mode_text_sensor_{nullptr};
#endif
  Sensor *bus_target_temperature_sensor_{nullptr};
  Sensor *unknown2_sensor_{nullptr};
  Sensor *mode_flags_sensor_{nullptr};
  Sensor *operation_flags_sensor_{nullptr};
  Sensor *ccm_error_flags_sensor_{nullptr};
  Sensor *unknown4_sensor_{nullptr};
  Sensor *unknown5_sensor_{nullptr};
  Sensor *unknown6_sensor_{nullptr};
  Sensor *c4_indoor_fan_pwm_sensor_{nullptr};
  Sensor *c4_indoor_fan_tach_sensor_{nullptr};
  Sensor *c4_compressor_flags_sensor_{nullptr};
  Sensor *c4_esp_profile_sensor_{nullptr};
  Sensor *c4_protection_flags_sensor_{nullptr};
  Sensor *c4_coil_inlet_sensor_{nullptr};
  Sensor *c4_coil_outlet_sensor_{nullptr};
  Sensor *c4_discharge_temp_sensor_{nullptr};
  Sensor *c4_expansion_valve_sensor_{nullptr};
  Sensor *c4_system_status_flags_sensor_{nullptr};
  Sensor *c4_target_fan_mode_sensor_{nullptr};
  Sensor *c4_compressor_frequency_sensor_{nullptr};
  Sensor *c4_subsystem_compressor_sensor_{nullptr};
  Sensor *c4_subsystem_outdoor_fan_sensor_{nullptr};
  Sensor *c4_subsystem_4way_valve_sensor_{nullptr};
  Sensor *c4_subsystem_inverter_sensor_{nullptr};
  StaticPressureNumber *static_pressure_number_{nullptr};
  ClimateMode last_on_mode_;
  float internal_temperature_{NAN};
  /// HEAT or COOL sub-mode last sent while CLIMATE_MODE_HEAT_COOL is selected.
  OperationMode auto_bus_mode_{OperationMode::COOL};

  void ParseResponse();
  float get_effective_current_temperature_() const;
  OperationMode get_bus_operation_mode_() const;
  void sync_auto_bus_mode_();
  uint8_t CalculateSetTime(uint32_t time);
  uint32_t CalculateGetTime(uint8_t time);
  void update_current_temperature_from_sensors_(bool &need_publish);
  void on_follow_me_sensor_update_(float state);
  void request_set_();
  void request_follow_me_();
  void advance_control_state_(uint8_t cmd_sent);

#ifdef USE_ZIGBEE
  zigbee::ZigbeeComponent *zigbee_{nullptr};
  ZbControlRequest zb_req_;
  void update_zigbee_thermostat_attrs_();
  static esp_err_t zb_thermostat_action_handler_(esp_zb_core_action_callback_id_t cb_id, const void *msg);
#endif
};

}  // namespace xye
}  // namespace midea
}  // namespace esphome
