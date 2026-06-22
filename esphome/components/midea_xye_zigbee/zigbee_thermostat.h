#pragma once
// Zigbee HA Thermostat (0x0201) + Fan Control (0x0202) bridge for midea_xye_zigbee.
//
// ONLY #include this from climate_midea_xye.cpp — it must be compiled in exactly
// one translation unit or you will get multiple-definition linker errors.
//
// Architecture
// ────────────
// register_zigbee_thermostat(zb)  — called from Python-generated setup code
//   (before App.setup()).  Builds cluster lists with full default attributes and
//   calls ZigbeeComponent::create_endpoint() so the thermostat endpoint is in the
//   esp_zb_ep_list before esp_zb_device_register() runs in ZigbeeComponent::setup().
//
// ClimateMideaXYE::setup()         — registers our action handler AFTER the
//   ZigbeeComponent's setup() has run (our component has lower priority = -20).
//   This intentionally overwrites ESPHome's default handler which only logs.
//
// zb_thermostat_action_handler_()  — static callback invoked by the ESP Zigbee
//   task when Z2M writes a thermostat/fan attribute.  Sets atomic flags on
//   ZbControlRequest and calls enable_loop_soon_any_context() so the ESPHome
//   main loop processes the command thread-safely.
//
// ClimateMideaXYE::loop()          — drains ZbControlRequest and calls control().
//
// update_zigbee_thermostat_attrs_() — pushes current climate state into the
//   registered Zigbee attributes after every publish_state().

#ifdef USE_ESP_IDF
#ifdef USE_ZIGBEE

#include <atomic>
#include <climits>

#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

#include "esphome/components/climate/climate.h"

namespace esphome {
namespace midea {
namespace xye {

// ── Thermostat endpoint and cluster / attribute identifiers ────────────────
//
// Endpoint 10 sits outside the range ESPHome auto-assigns to binary_sensor
// entities (1-N, max 8 via CONF_MAX_EP_NUMBER in zigbee_ep_esp32.py).
// Endpoint 100 — chosen to be safely above the range ESPHome auto-assigns
// to sensor/binary_sensor entities (which start at 1 and count up).
// With 24 diagnostic sub-sensors, ESPHome would use 1-24 and collide with 10.
static constexpr uint8_t  ZB_THERMO_EP          = 100;
static constexpr uint16_t ZB_CL_THERMOSTAT      = 0x0201;
static constexpr uint16_t ZB_CL_FAN_CONTROL     = 0x0202;

// Thermostat cluster (0x0201) attribute IDs
static constexpr uint16_t ZB_ATTR_LOCAL_TEMP    = 0x0000;  // int16s, 0.01 °C
static constexpr uint16_t ZB_ATTR_COOLING_SP    = 0x0011;  // int16s, 0.01 °C (writable)
static constexpr uint16_t ZB_ATTR_HEATING_SP    = 0x0012;  // int16s, 0.01 °C (writable)
static constexpr uint16_t ZB_ATTR_CTRL_SEQ      = 0x001B;  // enum8, mandatory
static constexpr uint16_t ZB_ATTR_SYSTEM_MODE   = 0x001C;  // enum8 (writable)

// Fan Control cluster (0x0202) attribute IDs
static constexpr uint16_t ZB_ATTR_FAN_MODE      = 0x0000;  // enum8 (writable)
static constexpr uint16_t ZB_ATTR_FAN_MODE_SEQ  = 0x0001;  // enum8

// ── Zigbee ↔ ESPHome mode translations ────────────────────────────────────

static uint8_t climate_mode_to_zb(climate::ClimateMode m) {
  switch (m) {
    case climate::CLIMATE_MODE_COOL:       return 0x03;
    case climate::CLIMATE_MODE_HEAT:       return 0x04;
    case climate::CLIMATE_MODE_HEAT_COOL:  return 0x01;
    case climate::CLIMATE_MODE_FAN_ONLY:   return 0x09;
    case climate::CLIMATE_MODE_DRY:        return 0x07;
    default:                               return 0x00;
  }
}

static climate::ClimateMode zb_to_climate_mode(uint8_t m) {
  switch (m) {
    case 0x01: return climate::CLIMATE_MODE_HEAT_COOL;
    case 0x03: return climate::CLIMATE_MODE_COOL;
    case 0x04: return climate::CLIMATE_MODE_HEAT;
    case 0x07: return climate::CLIMATE_MODE_DRY;
    case 0x09: return climate::CLIMATE_MODE_FAN_ONLY;
    default:   return climate::CLIMATE_MODE_OFF;
  }
}

static uint8_t fan_mode_to_zb(climate::ClimateFanMode f) {
  switch (f) {
    case climate::CLIMATE_FAN_LOW:    return 0x01;
    case climate::CLIMATE_FAN_MEDIUM: return 0x02;
    case climate::CLIMATE_FAN_HIGH:   return 0x03;
    case climate::CLIMATE_FAN_AUTO:   return 0x05;
    default:                          return 0x00;
  }
}

static climate::ClimateFanMode zb_to_fan_mode(uint8_t f) {
  switch (f) {
    case 0x01: return climate::CLIMATE_FAN_LOW;
    case 0x02: return climate::CLIMATE_FAN_MEDIUM;
    case 0x03: return climate::CLIMATE_FAN_HIGH;
    case 0x05: return climate::CLIMATE_FAN_AUTO;
    default:   return climate::CLIMATE_FAN_AUTO;
  }
}

// ── Cross-task command queue (Zigbee task → ESPHome main loop) ─────────────
//
// Written by the Zigbee-task action handler, consumed by ClimateMideaXYE::loop().
// INT16_MIN / -1 = "no change" sentinels.
struct ZbControlRequest {
  std::atomic<bool>    pending{false};
  std::atomic<int8_t>  mode_req{-1};          // -1 = unchanged, else ClimateMode cast
  std::atomic<int16_t> setpoint_raw{INT16_MIN}; // INT16_MIN = unchanged, else 0.01 °C
  std::atomic<int8_t>  fan_req{-1};           // -1 = unchanged, else ClimateFanMode cast
};

}  // namespace xye
}  // namespace midea
}  // namespace esphome

#endif  // USE_ZIGBEE
#endif  // USE_ESP_IDF
