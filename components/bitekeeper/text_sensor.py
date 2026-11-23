"""bitekeeper text sensors.

status        Connected / Disconnected
paired_name   name of the paired receiver
paired_mac    its MAC address
alarms        "1: 3.92 V online; 3: 3.85 V offline" (every paired alarm)
last_event    "2: bite" / "3: drop" — the last event
scan_results  receivers found by the last scan: "MAC name -70 dBm; …"
"""

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

BiteKeeperTextSensor = bitekeeper_ns.class_("BiteKeeperTextSensor", text_sensor.TextSensor, cg.Component)
BkTextType = bitekeeper_ns.enum("BkTextType", is_class=True)
POLLED = {
    "last_event": BkTextType.LAST_EVENT,
    "scan_results": BkTextType.SCAN_RESULTS,
    "paired_mac": BkTextType.PAIRED_MAC,
}

_ID = {cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper)}
_BASE = text_sensor.text_sensor_schema().extend(_ID)


def _polled(**kw):
    return text_sensor.text_sensor_schema(BiteKeeperTextSensor, **kw).extend(_ID).extend(cv.COMPONENT_SCHEMA)


CONFIG_SCHEMA = cv.typed_schema(
    {
        "status": _BASE,
        "paired_name": _BASE,
        "alarms": _BASE,
        "last_event": _polled(icon="mdi:fish"),
        "scan_results": _polled(icon="mdi:bluetooth-audio", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
        "paired_mac": _polled(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    },
    key=CONF_TYPE,
    lower=True,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_BITEKEEPER_ID])
    ts = await text_sensor.new_text_sensor(config)
    t = config[CONF_TYPE]
    if t == "status":
        cg.add(parent.set_status_text_sensor(ts))
    elif t == "paired_name":
        cg.add(parent.set_paired_name_text_sensor(ts))
    elif t == "alarms":
        cg.add(parent.set_alarms_text_sensor(ts))
    else:
        await cg.register_component(ts, config)
        await cg.register_parented(ts, config[CONF_BITEKEEPER_ID])
        cg.add(ts.set_type(POLLED[t]))
