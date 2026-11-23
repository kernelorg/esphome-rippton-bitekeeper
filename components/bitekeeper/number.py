"""bitekeeper numbers.

type: poll_interval (default)  keep-alive period in s, 0 = off; saved
type: alarm_volume       alarm: N   0..9
type: alarm_tone         alarm: N   0..8
type: alarm_sensitivity  alarm: N   0..9
type: alarm_color        alarm: N   0..5 — one of the alarm's 6 stored colours
type: receiver_volume               0..9

The setting entities show the cube's current value and send a change.
"""

import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_RESTORE_VALUE, CONF_TYPE, ENTITY_CATEGORY_CONFIG, UNIT_SECOND

from . import CONF_ALARM, CONF_BITEKEEPER_ID, BiteKeeper, bitekeeper_ns

DEPENDENCIES = ["bitekeeper"]

BiteKeeperNumber = bitekeeper_ns.class_("BiteKeeperNumber", number.Number, cg.Component)
SettingNumber = bitekeeper_ns.class_("BiteKeeperSettingNumber", number.Number, cg.Component)
BkSettingType = bitekeeper_ns.enum("BkSettingType", is_class=True)

_ID = {cv.GenerateID(CONF_BITEKEEPER_ID): cv.use_id(BiteKeeper)}
_ALARM = {cv.Required(CONF_ALARM): cv.int_range(min=1, max=8)}

# type: (enum, max value, icon, needs alarm)
SETTINGS = {
    "alarm_volume": (BkSettingType.ALARM_VOLUME, 9, "mdi:volume-high", True),
    "alarm_tone": (BkSettingType.ALARM_TONE, 8, "mdi:music-note", True),
    "alarm_sensitivity": (BkSettingType.ALARM_SENSITIVITY, 9, "mdi:tune-vertical", True),
    "alarm_color": (BkSettingType.ALARM_COLOR, 5, "mdi:palette", True),
    "receiver_volume": (BkSettingType.RECEIVER_VOLUME, 9, "mdi:volume-high", False),
}


def _setting_schema(icon, needs_alarm):
    return (
        number.number_schema(SettingNumber, icon=icon, entity_category=ENTITY_CATEGORY_CONFIG)
        .extend({**_ID, **(_ALARM if needs_alarm else {})})
        .extend(cv.COMPONENT_SCHEMA)
    )


CONFIG_SCHEMA = cv.typed_schema(
    {
        "poll_interval": number.number_schema(
            BiteKeeperNumber,
            icon="mdi:timer-refresh-outline",
            unit_of_measurement=UNIT_SECOND,
            entity_category=ENTITY_CATEGORY_CONFIG,
        )
        .extend({**_ID, cv.Optional(CONF_RESTORE_VALUE, default=True): cv.boolean})
        .extend(cv.COMPONENT_SCHEMA),
        **{name: _setting_schema(icon, alarm) for name, (_, _, icon, alarm) in SETTINGS.items()},
    },
    key=CONF_TYPE,
    default_type="poll_interval",
    lower=True,
)


async def to_code(config):
    t = config[CONF_TYPE]
    if t == "poll_interval":
        var = await number.new_number(config, min_value=0, max_value=300, step=1)
        await cg.register_component(var, config)
        await cg.register_parented(var, config[CONF_BITEKEEPER_ID])
        cg.add(var.set_restore_value(config[CONF_RESTORE_VALUE]))
        return
    kind, max_value, _, needs_alarm = SETTINGS[t]
    var = await number.new_number(config, min_value=0, max_value=max_value, step=1)
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_BITEKEEPER_ID])
    cg.add(var.set_type(kind))
    if needs_alarm:
        cg.add(var.set_alarm(config[CONF_ALARM]))
