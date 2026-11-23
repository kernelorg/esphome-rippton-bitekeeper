"""bitekeeper binary sensors.

type: connected (default) — the link to the cube is up.
type: alarm_online, alarm: N — alarm N is online (unknown while no alarm is
paired in that position).
"""

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, DEVICE_CLASS_CONNECTIVITY

from . import CONF_ALARM, CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

AlarmBinarySensor = bitekeeper_ns.class_(
    "BiteKeeperAlarmBinarySensor", binary_sensor.BinarySensor, cg.Component
)
_BASE = {cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper)}

CONFIG_SCHEMA = cv.typed_schema(
    {
        "connected": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_CONNECTIVITY).extend(_BASE),
        "alarm_online": binary_sensor.binary_sensor_schema(AlarmBinarySensor, device_class=DEVICE_CLASS_CONNECTIVITY)
        .extend({**_BASE, cv.Required(CONF_ALARM): cv.int_range(min=1, max=8)})
        .extend(cv.COMPONENT_SCHEMA),
    },
    key=CONF_TYPE,
    default_type="connected",
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_BITEKEEPER_ID])
    bs = await binary_sensor.new_binary_sensor(config)
    if config[CONF_TYPE] == "connected":
        cg.add(parent.set_connected_binary_sensor(bs))
        return
    await cg.register_component(bs, config)
    await cg.register_parented(bs, config[CONF_BITEKEEPER_ID])
    cg.add(bs.set_alarm(config[CONF_ALARM]))
