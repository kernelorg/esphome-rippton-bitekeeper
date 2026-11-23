"""bitekeeper event: "bite" / "drop" of one alarm, several or all of them.

    event:
      - platform: bitekeeper
        alarm: 2            # optional: one number or a list; omitted = every alarm
        name: "Rod 2"

Home Assistant automations use it as an event entity (event types bite, drop).
"""

import esphome.codegen as cg
from esphome.components import event
import esphome.config_validation as cv

from . import CONF_ALARM, CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

BiteKeeperEvent = bitekeeper_ns.class_("BiteKeeperEvent", event.Event, cg.Component)

CONFIG_SCHEMA = (
    event.event_schema(BiteKeeperEvent, icon="mdi:fish")
    .extend(
        {
            cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper),
            cv.Optional(CONF_ALARM): cv.ensure_list(cv.int_range(min=1, max=8)),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await event.new_event(config, event_types=["bite", "drop"])
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_BITEKEEPER_ID])
    mask = 0
    for n in config.get(CONF_ALARM, []):
        mask |= 1 << (n - 1)
    cg.add(var.set_alarms(mask))
