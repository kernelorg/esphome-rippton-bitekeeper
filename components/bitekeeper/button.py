"""bitekeeper buttons: scan (look for receivers), pair (the strongest one
found), forget (the paired receiver), poll (send the keep-alive now)."""

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_CONFIG

from . import CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

BiteKeeperButton = bitekeeper_ns.class_("BiteKeeperButton", button.Button, cg.Component)
BkButtonType = bitekeeper_ns.enum("BkButtonType", is_class=True)
TYPES = {
    "scan": (BkButtonType.SCAN, "mdi:bluetooth-audio"),
    "pair": (BkButtonType.PAIR, "mdi:link-variant"),
    "forget": (BkButtonType.FORGET, "mdi:link-variant-off"),
    "poll": (BkButtonType.POLL, "mdi:refresh"),
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        name: button.button_schema(BiteKeeperButton, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG)
        .extend({cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper)})
        .extend(cv.COMPONENT_SCHEMA)
        for name, (_, icon) in TYPES.items()
    },
    key=CONF_TYPE,
    lower=True,
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_BITEKEEPER_ID])
    cg.add(var.set_type(TYPES[config[CONF_TYPE]][0]))
