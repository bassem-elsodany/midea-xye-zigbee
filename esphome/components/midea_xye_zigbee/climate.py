from esphome.core import coroutine
from esphome import automation
from esphome.components import binary_sensor, climate, sensor, text_sensor, uart, remote_transmitter, number
from esphome.components.remote_base import CONF_TRANSMITTER_ID
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import (
    CONF_AUTOCONF,
    CONF_BEEPER,
    CONF_CUSTOM_FAN_MODES,
    CONF_CUSTOM_PRESETS,
    CONF_ID,
    CONF_PERIOD,
    CONF_SUPPORTED_MODES,
    CONF_SUPPORTED_PRESETS,
    CONF_SUPPORTED_SWING_MODES,
    CONF_TIMEOUT,
    CONF_TEMPERATURE,
    CONF_USE_FAHRENHEIT,
    CONF_MAX_VALUE,
    CONF_MIN_VALUE,
    CONF_ICON,
    CONF_MODE,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_EMPTY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_POWER,
    ICON_THERMOMETER,
    ICON_WATER_PERCENT,
    ICON_TIMER,
    ICON_BUG,
    ICON_SECURITY,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
    UNIT_WATT,
    UNIT_AMPERE,
    UNIT_MINUTE,
    UNIT_EMPTY,
)
from esphome.components.climate import (
    ClimateMode,
    ClimatePreset,
    ClimateSwingMode,
)

#CODEOWNERS = ["@dudanov"]
# ESP-IDF only — see esphome/components/midea_xye_zigbee/README.md
# "zigbee" dependency ensures esp-zigbee-sdk is available for the thermostat cluster.
DEPENDENCIES = ["climate", "uart", "zigbee"]
AUTO_LOAD = ["binary_sensor", "number", "sensor", "text_sensor"]
CONF_OUTDOOR_TEMPERATURE = "outdoor_temperature"
CONF_TEMPERATURE_2A = "temperature_2a"
CONF_TEMPERATURE_2B = "temperature_2b"
CONF_TEMPERATURE_3 = "temperature_3"
CONF_CURRENT = "current"
CONF_TIMER_START = "timer_start"
CONF_TIMER_STOP = "timer_stop"
CONF_ERROR_FLAGS = "error_flags"
CONF_PROTECT_FLAGS = "protect_flags"
CONF_POWER_USAGE = "power_usage"
CONF_HUMIDITY_SETPOINT = "humidity_setpoint"
CONF_STATIC_PRESSURE = "static_pressure"
CONF_FOLLOW_ME_SENSOR = "follow_me_sensor"
CONF_INTERNAL_CURRENT_TEMPERATURE = "internal_current_temperature"
CONF_DEFROST = "defrost"
CONF_COMPRESSOR_ACTIVE = "compressor_active"
CONF_FAN_SPEED = "fan_speed"
CONF_COMPRESSOR_AWARE_ACTION = "compressor_aware_action"
CONF_SYNC_FAN_MODE_FROM_DEVICE = "sync_fan_mode_from_device"
CONF_SYNC_FAN_MODE_FROM_C0 = "sync_fan_mode_from_c0"
CONF_UNIT_ID = "unit_id"
CONF_MASTER_ID = "master_id"
# C0 QUERY response trace fields (PROTOCOL.md bytes 6-29)
CONF_UNKNOWN1 = "unknown1"
CONF_CAPABILITIES = "capabilities"
CONF_CAPABILITIES_TEXT = "capabilities_text"
CONF_BUS_OPERATION_MODE = "bus_operation_mode"
CONF_BUS_OPERATION_MODE_TEXT = "bus_operation_mode_text"
CONF_BUS_FAN_MODE = "bus_fan_mode"
CONF_BUS_FAN_MODE_TEXT = "bus_fan_mode_text"
CONF_BUS_TARGET_TEMPERATURE = "bus_target_temperature"
CONF_UNKNOWN2 = "unknown2"
CONF_MODE_FLAGS = "mode_flags"
CONF_OPERATION_FLAGS = "operation_flags"
CONF_CCM_ERROR_FLAGS = "ccm_error_flags"
CONF_UNKNOWN4 = "unknown4"
CONF_UNKNOWN5 = "unknown5"
CONF_UNKNOWN6 = "unknown6"
# C4 QUERY_EXTENDED trace fields
CONF_C4_INDOOR_FAN_PWM = "c4_indoor_fan_pwm"
CONF_C4_INDOOR_FAN_TACH = "c4_indoor_fan_tach"
CONF_C4_COMPRESSOR_FLAGS = "c4_compressor_flags"
CONF_C4_ESP_PROFILE = "c4_esp_profile"
CONF_C4_PROTECTION_FLAGS = "c4_protection_flags"
CONF_C4_COIL_INLET = "c4_coil_inlet"
CONF_C4_COIL_OUTLET = "c4_coil_outlet"
CONF_C4_DISCHARGE_TEMP = "c4_discharge_temp"
CONF_C4_EXPANSION_VALVE = "c4_expansion_valve"
CONF_C4_SYSTEM_STATUS_FLAGS = "c4_system_status_flags"
CONF_C4_TARGET_FAN_MODE = "c4_target_fan_mode"
CONF_C4_COMPRESSOR_FREQUENCY = "c4_compressor_frequency"
CONF_C4_SUBSYSTEM_COMPRESSOR = "c4_subsystem_compressor"
CONF_C4_SUBSYSTEM_OUTDOOR_FAN = "c4_subsystem_outdoor_fan"
CONF_C4_SUBSYSTEM_4WAY_VALVE = "c4_subsystem_4way_valve"
CONF_C4_SUBSYSTEM_INVERTER = "c4_subsystem_inverter"

DIAGNOSTIC_BYTE_SCHEMA = sensor.sensor_schema(
    icon="mdi:code-braces",
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)
DIAGNOSTIC_TEXT_SCHEMA = text_sensor.text_sensor_schema(
    icon="mdi:code-braces",
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
)
midea_xye_ns = cg.esphome_ns.namespace("midea").namespace("xye")
ClimateMideaXYE = midea_xye_ns.class_("ClimateMideaXYE", climate.Climate, cg.Component)
StaticPressureNumber = midea_xye_ns.class_("StaticPressureNumber", number.Number, cg.Component)
Capabilities = midea_xye_ns.namespace("Constants")

# ZigbeeComponent reference — used to wire register_zigbee_thermostat().
# Declared here via the namespace so we don't pull in zigbee's Python package
# at module load time (avoids issues when building for non-Zigbee targets).
CONF_ZIGBEE_ID = "zigbee_id"
zigbee_ns = cg.esphome_ns.namespace("zigbee")
ZigbeeComponent = zigbee_ns.class_("ZigbeeComponent", cg.Component)

def templatize(value):
    if isinstance(value, cv.Schema):
        value = value.schema
    ret = {}
    for key, val in value.items():
        ret[key] = cv.templatable(val)
    return cv.Schema(ret)


def register_action(name, type_, schema):
    validator = templatize(schema).extend(MIDEA_ACTION_BASE_SCHEMA)
    registerer = automation.register_action(f"midea_xye_zigbee.{name}", type_, validator, synchronous=True)

    def decorator(func):
        async def new_func(config, action_id, template_arg, args):
            ac_ = await cg.get_variable(config[CONF_ID])
            var = cg.new_Pvariable(action_id, template_arg)
            cg.add(var.set_parent(ac_))
            await coroutine(func)(var, config, args)
            return var

        return registerer(new_func)

    return decorator


ALLOWED_CLIMATE_MODES = {
    "HEAT_COOL": ClimateMode.CLIMATE_MODE_HEAT_COOL,
    "COOL": ClimateMode.CLIMATE_MODE_COOL,
    "HEAT": ClimateMode.CLIMATE_MODE_HEAT,
    "DRY": ClimateMode.CLIMATE_MODE_DRY,
    "FAN_ONLY": ClimateMode.CLIMATE_MODE_FAN_ONLY,
}

ALLOWED_CLIMATE_PRESETS = {
    "ECO": ClimatePreset.CLIMATE_PRESET_ECO,
    "BOOST": ClimatePreset.CLIMATE_PRESET_BOOST,
    "SLEEP": ClimatePreset.CLIMATE_PRESET_SLEEP,
}

ALLOWED_CLIMATE_SWING_MODES = {
    "BOTH": ClimateSwingMode.CLIMATE_SWING_BOTH,
    "VERTICAL": ClimateSwingMode.CLIMATE_SWING_VERTICAL,
    "HORIZONTAL": ClimateSwingMode.CLIMATE_SWING_HORIZONTAL,
}

CUSTOM_FAN_MODES = {
    "SILENT": Capabilities.SILENT,
    "TURBO": Capabilities.TURBO,
}

CUSTOM_PRESETS = {
    "FREEZE_PROTECTION": Capabilities.FREEZE_PROTECTION,
}

validate_modes = cv.enum(ALLOWED_CLIMATE_MODES, upper=True)
validate_presets = cv.enum(ALLOWED_CLIMATE_PRESETS, upper=True)
validate_swing_modes = cv.enum(ALLOWED_CLIMATE_SWING_MODES, upper=True)
validate_custom_fan_modes = cv.enum(CUSTOM_FAN_MODES, upper=True)
validate_custom_presets = cv.enum(CUSTOM_PRESETS, upper=True)

CONFIG_SCHEMA = cv.All(
    climate.climate_schema(ClimateMideaXYE).extend(
        {
            cv.GenerateID(): cv.declare_id(ClimateMideaXYE),
            cv.Optional(CONF_PERIOD, default="1s"): cv.time_period,
            cv.Optional(CONF_TIMEOUT, default="100ms"): cv.time_period,
            cv.Optional(CONF_USE_FAHRENHEIT, default=False): cv.boolean,
            # Opt-in: derive the climate action from the C0 byte-19 compressor flag
            # and the defrost state. Off by default while byte 19 is still provisional;
            # legacy "fan running implies heating/cooling" behaviour is used when false.
            cv.Optional(CONF_COMPRESSOR_AWARE_ACTION, default=False): cv.boolean,
            # Opt-in: sync this->fan_mode from C4 target_fan_speed (commanded speed from the
            # physical thermostat), so Home Assistant reflects fan mode changes even when
            # not triggered by an HA command.
            cv.Optional(CONF_SYNC_FAN_MODE_FROM_DEVICE, default=False): cv.boolean,
            # Opt-in: sync this->fan_mode from the C0 fan_mode byte (physical running speed).
            # Useful when the unit does not support C4 extended query (returns 0xC5).
            # Logic: 0x00 → no update (fan idle); bit-7 set → AUTO; else → LOW/MEDIUM/HIGH.
            # Mutually exclusive with sync_fan_mode_from_device (C4 path takes precedence).
            cv.Optional(CONF_SYNC_FAN_MODE_FROM_C0, default=False): cv.boolean,
            # Destination/indoor-unit address used in outgoing frames. Default 0x00 matches the
            # protocol's typical single-unit value; use 0x00..0x3F to target a specific unit or
            # 0xFF to broadcast.
            cv.Optional(CONF_UNIT_ID, default=0x00): cv.Any(
                cv.int_range(min=0x00, max=0x3F), cv.int_(0xFF)
            ),
            # Source/master address advertised by this controller in outgoing frames (0x00..0x3F).
            cv.Optional(CONF_MASTER_ID, default=0x00): cv.int_range(min=0x00, max=0x3F),
            cv.OnlyWith(CONF_TRANSMITTER_ID, "remote_transmitter"): cv.use_id(
                remote_transmitter.RemoteTransmitterComponent
            ),
            cv.Optional(CONF_BEEPER, default=False): cv.boolean,
            # Zigbee thermostat cluster (0x0201 + 0x0202): wire the ESPHome ZigbeeComponent so
            # Zigbee2MQTT can read status and send mode/setpoint/fan commands.
            # Set this to the id: defined under the zigbee: block in your YAML.
            cv.Optional(CONF_ZIGBEE_ID): cv.use_id(ZigbeeComponent),
            cv.Optional(CONF_AUTOCONF, default=True): cv.boolean,
            cv.Optional(CONF_SUPPORTED_MODES): cv.ensure_list(validate_modes),
            cv.Optional(CONF_SUPPORTED_SWING_MODES): cv.ensure_list(
                validate_swing_modes
            ),
            cv.Optional(CONF_SUPPORTED_PRESETS): cv.ensure_list(validate_presets),
            cv.Optional(CONF_CUSTOM_PRESETS): cv.ensure_list(validate_custom_presets),
            cv.Optional(CONF_CUSTOM_FAN_MODES): cv.ensure_list(
                validate_custom_fan_modes
            ),
            cv.Optional(CONF_STATIC_PRESSURE): number.number_schema(StaticPressureNumber).extend({
                cv.GenerateID(): cv.declare_id(StaticPressureNumber),
                cv.Optional(CONF_MIN_VALUE, default=0): cv.float_,
                cv.Optional(CONF_MAX_VALUE, default=15): cv.float_,
                cv.Optional(CONF_ICON, default="mdi:gauge"): cv.icon,
                cv.Optional(CONF_MODE, default="BOX"): cv.enum(number.NUMBER_MODES, upper=True),
            }),
            cv.Optional(CONF_OUTDOOR_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            # Physical fan speed as a monotonic ordinal: 0=off, 1=low, 2=medium, 3=high.
            # The raw protocol nibble is not monotonic, so it is remapped before publishing.
            cv.Optional(CONF_FAN_SPEED): sensor.sensor_schema(
                icon="mdi:fan",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_TEMPERATURE_2A): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMPERATURE_2B): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TEMPERATURE_3): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                icon=ICON_POWER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TIMER_START): sensor.sensor_schema(
                unit_of_measurement=UNIT_MINUTE,
                icon=ICON_TIMER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_TIMER_STOP): sensor.sensor_schema(
                unit_of_measurement=UNIT_MINUTE,
                icon=ICON_TIMER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_ERROR_FLAGS): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_BUG,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_EMPTY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_PROTECT_FLAGS): sensor.sensor_schema(
                unit_of_measurement=UNIT_EMPTY,
                icon=ICON_SECURITY,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_EMPTY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),

            cv.Optional(CONF_POWER_USAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                icon=ICON_POWER,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_POWER,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_HUMIDITY_SETPOINT): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_WATER_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_HUMIDITY,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_FOLLOW_ME_SENSOR): cv.use_id(sensor.Sensor),
            cv.Optional(CONF_INTERNAL_CURRENT_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ),
            cv.Optional(CONF_DEFROST): binary_sensor.binary_sensor_schema(
                icon="mdi:snowflake-thermometer",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_COMPRESSOR_ACTIVE): binary_sensor.binary_sensor_schema(
                icon="mdi:engine",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # --- C0 QUERY (0xC0) receive trace (PROTOCOL.md bytes 6-29) ---
            cv.Optional(CONF_UNKNOWN1): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_CAPABILITIES): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_CAPABILITIES_TEXT): DIAGNOSTIC_TEXT_SCHEMA,
            cv.Optional(CONF_BUS_OPERATION_MODE): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_BUS_OPERATION_MODE_TEXT): DIAGNOSTIC_TEXT_SCHEMA,
            cv.Optional(CONF_BUS_FAN_MODE): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_BUS_FAN_MODE_TEXT): DIAGNOSTIC_TEXT_SCHEMA,
            cv.Optional(CONF_BUS_TARGET_TEMPERATURE): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_UNKNOWN2): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_MODE_FLAGS): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_OPERATION_FLAGS): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_CCM_ERROR_FLAGS): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_UNKNOWN4): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_UNKNOWN5): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_UNKNOWN6): DIAGNOSTIC_BYTE_SCHEMA,
            # --- C4 QUERY_EXTENDED (0xC4) receive trace ---
            cv.Optional(CONF_C4_INDOOR_FAN_PWM): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_INDOOR_FAN_TACH): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_COMPRESSOR_FLAGS): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_ESP_PROFILE): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_PROTECTION_FLAGS): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_COIL_INLET): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_C4_COIL_OUTLET): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_C4_DISCHARGE_TEMP): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_C4_EXPANSION_VALVE): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_SYSTEM_STATUS_FLAGS): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_TARGET_FAN_MODE): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_COMPRESSOR_FREQUENCY): sensor.sensor_schema(
                icon="mdi:sine-wave",
                accuracy_decimals=0,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_C4_SUBSYSTEM_COMPRESSOR): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_SUBSYSTEM_OUTDOOR_FAN): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_SUBSYSTEM_4WAY_VALVE): DIAGNOSTIC_BYTE_SCHEMA,
            cv.Optional(CONF_C4_SUBSYSTEM_INVERTER): DIAGNOSTIC_BYTE_SCHEMA,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_with_arduino,
)

# Actions
FollowMeAction = midea_xye_ns.class_("FollowMeAction", automation.Action)
DisplayToggleAction = midea_xye_ns.class_("DisplayToggleAction", automation.Action)
SwingStepAction = midea_xye_ns.class_("SwingStepAction", automation.Action)
BeeperOnAction = midea_xye_ns.class_("BeeperOnAction", automation.Action)
BeeperOffAction = midea_xye_ns.class_("BeeperOffAction", automation.Action)
PowerOnAction = midea_xye_ns.class_("PowerOnAction", automation.Action)
PowerOffAction = midea_xye_ns.class_("PowerOffAction", automation.Action)
PowerToggleAction = midea_xye_ns.class_("PowerToggleAction", automation.Action)

MIDEA_ACTION_BASE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.use_id(ClimateMideaXYE),
    }
)

# FollowMe action
MIDEA_FOLLOW_ME_MIN = 0
MIDEA_FOLLOW_ME_MAX = 37
MIDEA_FOLLOW_ME_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TEMPERATURE): cv.templatable(cv.temperature),
        cv.Optional(CONF_BEEPER, default=False): cv.templatable(cv.boolean),
    }
)


@register_action("follow_me", FollowMeAction, MIDEA_FOLLOW_ME_SCHEMA)
async def follow_me_to_code(var, config, args):
    template_ = await cg.templatable(config[CONF_BEEPER], args, cg.bool_)
    cg.add(var.set_beeper(template_))
    template_ = await cg.templatable(config[CONF_TEMPERATURE], args, cg.float_)
    cg.add(var.set_temperature(template_))


# Toggle Display action
@register_action(
    "display_toggle",
    DisplayToggleAction,
    cv.Schema({}),
)
async def display_toggle_to_code(var, config, args):
    pass


# Swing Step action
@register_action(
    "swing_step",
    SwingStepAction,
    cv.Schema({}),
)
async def swing_step_to_code(var, config, args):
    pass


# Beeper On action
@register_action(
    "beeper_on",
    BeeperOnAction,
    cv.Schema({}),
)
async def beeper_on_to_code(var, config, args):
    pass


# Beeper Off action
@register_action(
    "beeper_off",
    BeeperOffAction,
    cv.Schema({}),
)
async def beeper_off_to_code(var, config, args):
    pass


# Power On action
@register_action(
    "power_on",
    PowerOnAction,
    cv.Schema({}),
)
async def power_on_to_code(var, config, args):
    pass


# Power Off action
@register_action(
    "power_off",
    PowerOffAction,
    cv.Schema({}),
)
async def power_off_to_code(var, config, args):
    pass


# Power Toggle action
@register_action(
    "power_toggle",
    PowerToggleAction,
    cv.Schema({}),
)
async def power_inv_to_code(var, config, args):
    pass


async def to_code(config):
    var = await climate.new_climate(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    await climate.register_climate(var, config)
    cg.add(var.set_period(config[CONF_PERIOD].total_milliseconds))
    cg.add(var.set_response_timeout(config[CONF_TIMEOUT].total_milliseconds))
    cg.add(var.set_use_fahrenheit(config[CONF_USE_FAHRENHEIT]))
    cg.add(var.set_compressor_aware_action(config[CONF_COMPRESSOR_AWARE_ACTION]))
    cg.add(var.set_sync_fan_mode_from_device(config[CONF_SYNC_FAN_MODE_FROM_DEVICE]))
    cg.add(var.set_sync_fan_mode_from_c0(config[CONF_SYNC_FAN_MODE_FROM_C0]))
    cg.add(var.set_unit_id(config[CONF_UNIT_ID]))
    cg.add(var.set_master_id(config[CONF_MASTER_ID]))
    if CONF_TRANSMITTER_ID in config:
        cg.add_define("USE_REMOTE_TRANSMITTER")
        transmitter_ = await cg.get_variable(config[CONF_TRANSMITTER_ID])
        cg.add(var.set_transmitter(transmitter_))
    if CONF_SUPPORTED_MODES in config:
        cg.add(var.set_supported_modes(config[CONF_SUPPORTED_MODES]))
    if CONF_SUPPORTED_SWING_MODES in config:
        cg.add(var.set_supported_swing_modes(config[CONF_SUPPORTED_SWING_MODES]))
    if CONF_SUPPORTED_PRESETS in config:
        cg.add(var.set_supported_presets(config[CONF_SUPPORTED_PRESETS]))
    if CONF_CUSTOM_PRESETS in config:
        cg.add(var.set_custom_presets(config[CONF_CUSTOM_PRESETS]))
    if CONF_CUSTOM_FAN_MODES in config:
        cg.add(var.set_custom_fan_modes(config[CONF_CUSTOM_FAN_MODES]))
    if CONF_STATIC_PRESSURE in config:
        static_pressure_var = await number.new_number(
            config[CONF_STATIC_PRESSURE],
            min_value=config[CONF_STATIC_PRESSURE][CONF_MIN_VALUE],
            max_value=config[CONF_STATIC_PRESSURE][CONF_MAX_VALUE],
            step=1
        )
        cg.add(var.set_static_pressure_number(static_pressure_var))
    if CONF_OUTDOOR_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_OUTDOOR_TEMPERATURE])
        cg.add(var.set_outdoor_temperature_sensor(sens))
    if CONF_FAN_SPEED in config:
        sens = await sensor.new_sensor(config[CONF_FAN_SPEED])
        cg.add(var.set_fan_speed_sensor(sens))
    if CONF_TEMPERATURE_2A in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE_2A])
        cg.add(var.set_temperature_2a_sensor(sens))
    if CONF_TEMPERATURE_2B in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE_2B])
        cg.add(var.set_temperature_2b_sensor(sens))
    if CONF_TEMPERATURE_3 in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE_3])
        cg.add(var.set_temperature_3_sensor(sens))
    if CONF_CURRENT in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT])
        cg.add(var.set_current_sensor(sens))
    if CONF_TIMER_START in config:
        sens = await sensor.new_sensor(config[CONF_TIMER_START])
        cg.add(var.set_timer_start_sensor(sens))
    if CONF_TIMER_STOP in config:
        sens = await sensor.new_sensor(config[CONF_TIMER_STOP])
        cg.add(var.set_timer_stop_sensor(sens))
    if CONF_ERROR_FLAGS in config:
        sens = await sensor.new_sensor(config[CONF_ERROR_FLAGS])
        cg.add(var.set_error_flags_sensor(sens))
    if CONF_PROTECT_FLAGS in config:
        sens = await sensor.new_sensor(config[CONF_PROTECT_FLAGS])
        cg.add(var.set_protect_flags_sensor(sens))
    if CONF_POWER_USAGE in config:
        sens = await sensor.new_sensor(config[CONF_POWER_USAGE])
        cg.add(var.set_power_sensor(sens))
    if CONF_HUMIDITY_SETPOINT in config:
        sens = await sensor.new_sensor(config[CONF_HUMIDITY_SETPOINT])
        cg.add(var.set_humidity_setpoint_sensor(sens))
    if CONF_FOLLOW_ME_SENSOR in config:
        sens = await cg.get_variable(config[CONF_FOLLOW_ME_SENSOR])
        cg.add(var.set_follow_me_sensor(sens))
    if CONF_ZIGBEE_ID in config:
        zb = await cg.get_variable(config[CONF_ZIGBEE_ID])
        cg.add(var.register_zigbee_thermostat(zb))
    if CONF_INTERNAL_CURRENT_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_INTERNAL_CURRENT_TEMPERATURE])
        cg.add(var.set_internal_current_temperature_sensor(sens))
    if CONF_DEFROST in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_DEFROST])
        cg.add(var.set_defrost_sensor(sens))
    if CONF_COMPRESSOR_ACTIVE in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_COMPRESSOR_ACTIVE])
        cg.add(var.set_compressor_active_sensor(sens))
    trace_sensors = [
        (CONF_UNKNOWN1, "set_unknown1_sensor"),
        (CONF_CAPABILITIES, "set_capabilities_sensor"),
        (CONF_BUS_OPERATION_MODE, "set_bus_operation_mode_sensor"),
        (CONF_BUS_FAN_MODE, "set_bus_fan_mode_sensor"),
        (CONF_BUS_TARGET_TEMPERATURE, "set_bus_target_temperature_sensor"),
        (CONF_UNKNOWN2, "set_unknown2_sensor"),
        (CONF_MODE_FLAGS, "set_mode_flags_sensor"),
        (CONF_OPERATION_FLAGS, "set_operation_flags_sensor"),
        (CONF_CCM_ERROR_FLAGS, "set_ccm_error_flags_sensor"),
        (CONF_UNKNOWN4, "set_unknown4_sensor"),
        (CONF_UNKNOWN5, "set_unknown5_sensor"),
        (CONF_UNKNOWN6, "set_unknown6_sensor"),
        (CONF_C4_INDOOR_FAN_PWM, "set_c4_indoor_fan_pwm_sensor"),
        (CONF_C4_INDOOR_FAN_TACH, "set_c4_indoor_fan_tach_sensor"),
        (CONF_C4_COMPRESSOR_FLAGS, "set_c4_compressor_flags_sensor"),
        (CONF_C4_ESP_PROFILE, "set_c4_esp_profile_sensor"),
        (CONF_C4_PROTECTION_FLAGS, "set_c4_protection_flags_sensor"),
        (CONF_C4_COIL_INLET, "set_c4_coil_inlet_sensor"),
        (CONF_C4_COIL_OUTLET, "set_c4_coil_outlet_sensor"),
        (CONF_C4_DISCHARGE_TEMP, "set_c4_discharge_temp_sensor"),
        (CONF_C4_EXPANSION_VALVE, "set_c4_expansion_valve_sensor"),
        (CONF_C4_SYSTEM_STATUS_FLAGS, "set_c4_system_status_flags_sensor"),
        (CONF_C4_TARGET_FAN_MODE, "set_c4_target_fan_mode_sensor"),
        (CONF_C4_COMPRESSOR_FREQUENCY, "set_c4_compressor_frequency_sensor"),
        (CONF_C4_SUBSYSTEM_COMPRESSOR, "set_c4_subsystem_compressor_sensor"),
        (CONF_C4_SUBSYSTEM_OUTDOOR_FAN, "set_c4_subsystem_outdoor_fan_sensor"),
        (CONF_C4_SUBSYSTEM_4WAY_VALVE, "set_c4_subsystem_4way_valve_sensor"),
        (CONF_C4_SUBSYSTEM_INVERTER, "set_c4_subsystem_inverter_sensor"),
    ]
    for conf_key, setter in trace_sensors:
        if conf_key in config:
            sens = await sensor.new_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))
    trace_text_sensors = [
        (CONF_CAPABILITIES_TEXT, "set_capabilities_text_sensor"),
        (CONF_BUS_OPERATION_MODE_TEXT, "set_bus_operation_mode_text_sensor"),
        (CONF_BUS_FAN_MODE_TEXT, "set_bus_fan_mode_text_sensor"),
    ]
    for conf_key, setter in trace_text_sensors:
        if conf_key in config:
            sens = await text_sensor.new_text_sensor(config[conf_key])
            cg.add(getattr(var, setter)(sens))
