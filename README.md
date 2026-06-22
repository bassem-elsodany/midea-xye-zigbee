# midea-xye-zigbee

ESPHome external component for **Midea HVAC units** over the XYE RS-485 bus,
targeting **ESP32-C6 / ESP32-H2** with full **Zigbee2MQTT thermostat control**.

## Features

- Full Midea XYE RS-485 protocol support (read status + send commands)
- Zigbee HA Thermostat cluster **0x0201** — mode, setpoint, local temperature
- Zigbee Fan Control cluster **0x0202** — fan speed
- Zigbee2MQTT auto-discovery: mode selector, temperature slider, fan selector
- ESP-IDF only (no Arduino dependency)
- No Wi-Fi required — pure Zigbee end device

## Supported boards

| Board | Works |
|---|---|
| ESP32-C6-DevKitC-1 | Yes |
| ESP32-H2-DevKitM-1 | Yes |
| ESP32 / ESP8266 | No (use [hvac-midea-xye](https://github.com/bassem-elsodany/hvac-midea-xye) instead) |

## Quick start

```yaml
esp32:
  board: esp32-c6-devkitc-1
  variant: esp32c6
  framework:
    type: esp-idf

external_components:
  - source:
      type: git
      url: https://github.com/bassem-elsodany/midea-xye-zigbee
    components: [midea_xye_zigbee]

zigbee:
  id: zb_hvac
  router: false
  power_source: MAINS_SINGLE_PHASE
  model: midea-xye-hvac

uart:
  id: xye_uart
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

climate:
  - platform: midea_xye_zigbee
    zigbee_id: zb_hvac
    name: "Living Room AC"
    uart_id: xye_uart
    unit_id: 1
    master_id: 0
```

See [`config/zigbee-hvac-masterbedroom.yaml`](config/zigbee-hvac-masterbedroom.yaml) for a complete example with diagnostic sensors.

## Zigbee cluster mapping

| Attribute | ZCL | Direction |
|---|---|---|
| System mode (off/cool/heat/dry/fan/auto) | 0x0201 / 0x001C | Read + Write |
| Cooling setpoint | 0x0201 / 0x0011 | Read + Write |
| Heating setpoint | 0x0201 / 0x0012 | Read + Write |
| Local temperature | 0x0201 / 0x0000 | Read only |
| Fan mode (low/med/high/auto) | 0x0202 / 0x0000 | Read + Write |

## Pairing with Zigbee2MQTT

1. Enable **Permit join** in Z2M
2. Power-cycle (or factory-reset) the ESP32-C6
3. Z2M discovers the device and interviews endpoint 10
4. Climate controls appear automatically in Z2M and Home Assistant

> If you previously paired this device with older firmware, **remove it in Z2M first** so it re-interviews and discovers the thermostat endpoint.

## Component structure

```
esphome/components/midea_xye_zigbee/   ← ESPHome external component
config/
  zigbee-hvac-masterbedroom.yaml       ← full example config
  zigbee-idf/                          ← minimal ESP-IDF only example
```

## Protocol reference

See [`esphome/components/midea_xye_zigbee/PROTOCOL.md`](esphome/components/midea_xye_zigbee/PROTOCOL.md) for the Midea XYE RS-485 bus protocol details.

## Related

- [hvac-midea-xye](https://github.com/bassem-elsodany/hvac-midea-xye) — Arduino / Wi-Fi version of this component
