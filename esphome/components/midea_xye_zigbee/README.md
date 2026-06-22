# midea_xye_zigbee

ESP-IDF port of `midea_xye` for **ESP32-C6** and **ESP32-H2** targets with
full **Zigbee2MQTT thermostat control** via standard Zigbee clusters.

## Status

| Item | Status |
|------|--------|
| Copy of `midea_xye` protocol + climate logic | Done |
| `USE_ARDUINO` guards removed | Done |
| ESP-IDF compile-time check | Done |
| `do_beeper_on()` / `do_beeper_off()` methods | Done (no-op — XYE RS-485 has no beeper bit) |
| Zigbee HA Thermostat cluster 0x0201 | **Done** |
| Zigbee Fan Control cluster 0x0202 | **Done** |
| Z2M: read mode / setpoint / fan / temp | **Done** |
| Z2M: write mode / setpoint / fan | **Done** |
| Example YAML (`config/zigbee-hvac-masterbedroom.yaml`) | Done |
| Hardware test on ESP32-C6 | **Needed — please test and report** |

## How Zigbee control works

```
Z2M → write 0x0201/system_mode → zb_thermostat_action_handler_()
                                 → ZbControlRequest (atomic)
                                 → enable_loop_soon_any_context()
                                 → ClimateMideaXYE::loop()
                                 → climate.make_call().set_mode().perform()
                                 → XYE RS-485 SET frame sent to AC unit

AC unit → C0 QUERY response → ParseResponse() → publish_state()
                              → update_zigbee_thermostat_attrs_()
                              → esp_zb_zcl_set_attribute_val(0x0201/local_temp …)
                              → Z2M reads updated attributes
```

Endpoint **10** is used for the thermostat (outside ESPHome's auto-assigned range
of 1-N for binary sensors). The endpoint is registered by calling
`ZigbeeComponent::create_endpoint()` before `App.setup()` so it is included in
`esp_zb_device_register()` together with ESPHome's own endpoints.

## Cluster mapping

| Zigbee attribute | Value | Notes |
|-----------------|-------|-------|
| `system_mode` (0x001C) | 0x00=Off 0x01=Auto 0x03=Cool 0x04=Heat 0x07=Dry 0x09=Fan | Read + Write |
| `occupied_cooling_setpoint` (0x0011) | int16s, 0.01 °C | Read + Write |
| `occupied_heating_setpoint` (0x0012) | int16s, 0.01 °C | Read + Write (same as cooling) |
| `local_temperature` (0x0000) | int16s, 0.01 °C | Read only (from C0 T1 or follow_me_sensor) |
| Fan `fan_mode` (0x0000) | 0x01=Low 0x02=Med 0x03=High 0x05=Auto | Read + Write |

## vs `midea_xye` (Arduino)

| | `midea_xye` | `midea_xye_zigbee` |
|---|-------------|-------------------|
| Framework | Arduino | **ESP-IDF only** |
| Boards | ESP32, ESP8266 | ESP32-C6, ESP32-H2 |
| WiFi | Yes | Optional (no WiFi in Zigbee-only config) |
| Zigbee sensor read | No | Yes (all C0 sensors via ESPHome `zigbee:`) |
| Zigbee climate control | No | **Yes** (endpoint 10, clusters 0x0201+0x0202) |

## Usage

```yaml
esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  framework:
    type: esp-idf

zigbee:
  id: zb_hvac
  router: false
  power_source: MAINS_SINGLE_PHASE
  model: midea-xye-hvac   # shown in Z2M device list

climate:
  - platform: midea_xye_zigbee
    zigbee_id: zb_hvac    # links thermostat cluster to the zigbee: block
    # ... same options as midea_xye
```

See [config/zigbee-hvac-masterbedroom.yaml](../../config/zigbee-hvac-masterbedroom.yaml)
for the full masterbedroom config.

## Re-interview in Z2M after flashing

After flashing a new build, Zigbee2MQTT must re-interview the device to discover
the thermostat endpoint.  Remove the device in Z2M, put the ESP into pairing mode
(factory reset or first boot), and re-pair.

## Notes

- `do_beeper_on()` / `do_beeper_off()` are no-ops.  The XYE RS-485 SET frame has
  no dedicated beeper bit; the beeper flag only applies to `do_follow_me(beeper=true)`
  which signals the AC unit via the FOLLOW_ME IR subcommand.
- Follow-Me via an HA Zigbee room sensor is **not available** in Zigbee-only builds
  (no WiFi / no ESPHome API).  Wire a local I2C sensor and set `follow_me_sensor`.
- OTA update is **USB only** (no Zigbee OTA on ESPHome ESP32 yet).

Protocol reference: [PROTOCOL.md](PROTOCOL.md)
