"""bitekeeper switches.

type: running (default)  scanning and the link to the cube (start / stop)
type: alarm_mute   alarm: N   alarm N muted (state from the cube)
type: drop_back    alarm: N   drop-back alarm of alarm N (state from the cube)
type: mute_all                mute everything (last command sent)
type: vibration               receiver vibration (last command sent)
type: drift                   drift alarm of every alarm (last command sent)

Nothing is switched at boot (default restore_mode DISABLED).
"""

import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_TYPE, ENTITY_CATEGORY_CONFIG

from . import CONF_ALARM, CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

BiteKeeperSwitch = bitekeeper_ns.class_("BiteKeeperSwitch", switch.Switch, cg.Component)
SettingSwitch = bitekeeper_ns.class_("BiteKeeperSettingSwitch", switch.Switch, cg.Component)
BkSwitchType = bitekeeper_ns.enum("BkSwitchType", is_class=True)

_ID = {cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper)}
_ALARM = {cv.Required(CONF_ALARM): cv.int_range(min=1, max=8)}

SETTINGS = {
    "alarm_mute": (BkSwitchType.ALARM_MUTE, "mdi:volume-off", True),
    "drop_back": (BkSwitchType.DROP_BACK, "mdi:arrow-collapse-down", True),
    "mute_all": (BkSwitchType.MUTE_ALL, "mdi:volume-off", False),
    "vibration": (BkSwitchType.VIBRATION, "mdi:vibrate", False),
    "drift": (BkSwitchType.DRIFT, "mdi:waves", False),
}


def _setting_schema(icon, needs_alarm):
    return (
        switch.switch_schema(SettingSwitch, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG,
                             default_restore_mode="DISABLED")
        .extend({**_ID, **(_ALARM if needs_alarm else {})})
        .extend(cv.COMPONENT_SCHEMA)
    )


CONFIG_SCHEMA = cv.typed_schema(
    {
        "running": switch.switch_schema(BiteKeeperSwitch, icon="mdi:bluetooth-connect", default_restore_mode="DISABLED")
        .extend(_ID)
        .extend(cv.COMPONENT_SCHEMA),
        **{name: _setting_schema(icon, alarm) for name, (_, icon, alarm) in SETTINGS.items()},
    },
    key=CONF_TYPE,
    default_type="running",
    lower=True,
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_BITEKEEPER_ID])
    t = config[CONF_TYPE]
    if t == "running":
        return
    kind, _, needs_alarm = SETTINGS[t]
    cg.add(var.set_type(kind))
    if needs_alarm:
        cg.add(var.set_alarm(config[CONF_ALARM]))
