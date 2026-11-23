import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TYPE,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_VOLT,
)

from . import CONF_ALARM, CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

CountSensor = bitekeeper_ns.class_("BiteKeeperCountSensor", sensor.Sensor, cg.Component)
DriftSensor = bitekeeper_ns.class_("BiteKeeperDriftSensor", sensor.Sensor, cg.Component)
UNIT_CENTIMETER = "cm"

_VOLTAGE = sensor.sensor_schema(
    unit_of_measurement=UNIT_VOLT,
    accuracy_decimals=3,
    device_class=DEVICE_CLASS_VOLTAGE,
    state_class=STATE_CLASS_MEASUREMENT,
)
_BASE = {cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper)}

CONFIG_SCHEMA = cv.typed_schema(
    {
        "receiver_voltage": _VOLTAGE.extend(_BASE),
        # Battery of alarm N (1..8). Reports unknown while no alarm is paired
        # in that position — see alarm_count / the "alarms" text sensor.
        "alarm_voltage": _VOLTAGE.extend(
            {**_BASE, cv.Required(CONF_ALARM): cv.int_range(min=1, max=8)}
        ),
        "drift": sensor.sensor_schema(
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=0,
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(_BASE),
        # Number of alarms the cube reports as paired (0..8).
        "alarm_count": sensor.sensor_schema(
            accuracy_decimals=0,
            icon="mdi:bell-ring-outline",
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(_BASE),
        # Drift distance of alarm N (1..8), cm.
        "alarm_drift": sensor.sensor_schema(
            DriftSensor,
            unit_of_measurement=UNIT_CENTIMETER,
            accuracy_decimals=0,
            icon="mdi:waves",
            state_class=STATE_CLASS_MEASUREMENT,
        )
        .extend({**_BASE, cv.Required(CONF_ALARM): cv.int_range(min=1, max=8)})
        .extend(cv.COMPONENT_SCHEMA),
        # Bites + drop-backs since boot.
        "event_count": sensor.sensor_schema(
            CountSensor,
            accuracy_decimals=0,
            icon="mdi:fish",
            state_class=STATE_CLASS_TOTAL_INCREASING,
        )
        .extend(_BASE)
        .extend(cv.COMPONENT_SCHEMA),
        "rssi": sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
        ).extend(_BASE),
    },
    key=CONF_TYPE,
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_BITEKEEPER_ID])
    sens = await sensor.new_sensor(config)
    t = config[CONF_TYPE]
    if t == "receiver_voltage":
        cg.add(parent.set_receiver_voltage_sensor(sens))
    elif t == "alarm_voltage":
        cg.add(parent.set_slot_voltage_sensor(config[CONF_ALARM] - 1, sens))
    elif t == "drift":
        cg.add(parent.set_drift_sensor(sens))
    elif t == "alarm_count":
        cg.add(parent.set_alarm_count_sensor(sens))
    elif t == "rssi":
        cg.add(parent.set_rssi_sensor(sens))
    elif t == "alarm_drift":
        await cg.register_component(sens, config)
        await cg.register_parented(sens, config[CONF_BITEKEEPER_ID])
        cg.add(sens.set_alarm(config[CONF_ALARM]))
    elif t == "event_count":
        await cg.register_component(sens, config)
        await cg.register_parented(sens, config[CONF_BITEKEEPER_ID])
