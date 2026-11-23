"""Bite Cube BLE receiver for ESPHome.

Connects to the cube over service FFF0 (FFF1 notify, FFF2 write), keeps the
stream alive with three data requests, parses the cube's MAVLink-v1 frames
(alarm events, receiver, alarm snapshot, drift, command acks — CRC checked),
raises bite / drop-back events and sends settings (COMMAND_INT).
"""

from esphome import automation
import esphome.codegen as cg
from esphome.components import ble_client, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID

CODEOWNERS = ["@kernelorg"]
DEPENDENCIES = ["ble_client", "esp32_ble_tracker"]
MULTI_CONF = True

CONF_BITEKEEPER_ID = "bitekeeper_id"
CONF_POLL_INTERVAL = "poll_interval"
CONF_CONNECT_DELAY = "connect_delay"
CONF_WATCHDOG_MIN = "watchdog_min"
CONF_STOP_SCAN_ON_CONNECT = "stop_scan_on_connect"
CONF_BITE_REPEAT = "bite_repeat"
CONF_ON_ALARM = "on_alarm"
CONF_ON_BITE = "on_bite"
CONF_ON_DROP = "on_drop"
CONF_ON_CONNECT = "on_connect"
CONF_ON_DISCONNECT = "on_disconnect"
CONF_ON_DATA = "on_data"
CONF_ON_ALARMS_CHANGE = "on_alarms_change"
CONF_ON_COMMAND_ACK = "on_command_ack"
CONF_AUTO_START = "auto_start"
CONF_MAC = "mac"
CONF_NAME = "name"
CONF_INTERVAL = "interval"
CONF_ALARM = "alarm"
CONF_DIRECTION = "direction"
DIRECTIONS = {"bite": 0, "drop": 1}

bitekeeper_ns = cg.esphome_ns.namespace("bitekeeper")
BiteKeeper = bitekeeper_ns.class_(
    "BiteKeeper",
    cg.Component,
    ble_client.BLEClientNode,
    esp32_ble_tracker.ESPBTDeviceListener,
)
AlarmTrigger = bitekeeper_ns.class_(
    "AlarmTrigger", automation.Trigger.template(cg.uint8, cg.uint8)
)
BiteTrigger = bitekeeper_ns.class_("BiteTrigger", automation.Trigger.template(cg.uint8))
DropTrigger = bitekeeper_ns.class_("DropTrigger", automation.Trigger.template(cg.uint8))
ConnectTrigger = bitekeeper_ns.class_("ConnectTrigger", automation.Trigger.template())
DisconnectTrigger = bitekeeper_ns.class_(
    "DisconnectTrigger", automation.Trigger.template()
)
DataTrigger = bitekeeper_ns.class_("DataTrigger", automation.Trigger.template())
CommandAckTrigger = bitekeeper_ns.class_(
    "CommandAckTrigger", automation.Trigger.template(cg.uint8, cg.uint8)
)
AlarmsChangeTrigger = bitekeeper_ns.class_(
    "AlarmsChangeTrigger", automation.Trigger.template(cg.uint8)
)

StartAction = bitekeeper_ns.class_("StartAction", automation.Action)
StopAction = bitekeeper_ns.class_("StopAction", automation.Action)
PollAction = bitekeeper_ns.class_("PollAction", automation.Action)
ForgetAction = bitekeeper_ns.class_("ForgetAction", automation.Action)
ScanClearAction = bitekeeper_ns.class_("ScanClearAction", automation.Action)
PairAction = bitekeeper_ns.class_("PairAction", automation.Action)
SetPollIntervalAction = bitekeeper_ns.class_("SetPollIntervalAction", automation.Action)
SetAlarmAction = bitekeeper_ns.class_("SetAlarmAction", automation.Action)
SetReceiverAction = bitekeeper_ns.class_("SetReceiverAction", automation.Action)
SetDriftAction = bitekeeper_ns.class_("SetDriftAction", automation.Action)
SendCommandAction = bitekeeper_ns.class_("SendCommandAction", automation.Action)


def _trigger(cls, extra=None):
    return automation.validate_automation(
        {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(cls), **(extra or {})}
    )


# Optional filter of on_bite / on_drop / on_alarm: one alarm number or a list.
_ALARM_FILTER = {cv.Optional(CONF_ALARM): cv.ensure_list(cv.int_range(min=1, max=8))}


def _alarm_mask(conf):
    mask = 0
    for n in conf.get(CONF_ALARM, []):
        mask |= 1 << (n - 1)
    return mask


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(BiteKeeper),
            # Start scanning/connecting as soon as the BLE stack is up. Set to
            # false when the config calls bitekeeper.start / stop itself.
            cv.Optional(CONF_AUTO_START, default=True): cv.boolean,
            # 0 disables the keep-alive (the cube then stops streaming after
            # ~30 s of idle and the watchdog reconnects).
            cv.Optional(
                CONF_POLL_INTERVAL, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_CONNECT_DELAY, default="1500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_WATCHDOG_MIN, default="30s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_STOP_SCAN_ON_CONNECT, default=True): cv.boolean,
            cv.Optional(
                CONF_BITE_REPEAT, default="1s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ON_ALARM): _trigger(
                AlarmTrigger,
                {**_ALARM_FILTER, cv.Optional(CONF_DIRECTION): cv.enum(DIRECTIONS, lower=True)},
            ),
            cv.Optional(CONF_ON_BITE): _trigger(BiteTrigger, _ALARM_FILTER),
            cv.Optional(CONF_ON_DROP): _trigger(DropTrigger, _ALARM_FILTER),
            cv.Optional(CONF_ON_CONNECT): _trigger(ConnectTrigger),
            cv.Optional(CONF_ON_DISCONNECT): _trigger(DisconnectTrigger),
            cv.Optional(CONF_ON_DATA): _trigger(DataTrigger),
            cv.Optional(CONF_ON_ALARMS_CHANGE): _trigger(AlarmsChangeTrigger),
            cv.Optional(CONF_ON_COMMAND_ACK): _trigger(CommandAckTrigger),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)

    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_connect_delay(config[CONF_CONNECT_DELAY]))
    cg.add(var.set_watchdog_min(config[CONF_WATCHDOG_MIN]))
    cg.add(var.set_stop_scan_on_connect(config[CONF_STOP_SCAN_ON_CONNECT]))
    cg.add(var.set_bite_repeat(config[CONF_BITE_REPEAT]))
    cg.add(var.set_auto_start(config[CONF_AUTO_START]))

    for conf in config.get(CONF_ON_ALARM, []):
        direction = conf[CONF_DIRECTION] if CONF_DIRECTION in conf else -1
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var, _alarm_mask(conf), direction)
        await automation.build_automation(
            trig, [(cg.uint8, "alarm"), (cg.uint8, "direction")], conf
        )
    for key in (CONF_ON_BITE, CONF_ON_DROP):
        for conf in config.get(key, []):
            trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var, _alarm_mask(conf))
            await automation.build_automation(trig, [(cg.uint8, "alarm")], conf)
    for key in (CONF_ON_CONNECT, CONF_ON_DISCONNECT, CONF_ON_DATA):
        for conf in config.get(key, []):
            trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(trig, [], conf)
    for conf in config.get(CONF_ON_COMMAND_ACK, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.uint8, "command"), (cg.uint8, "status")], conf)
    for conf in config.get(CONF_ON_ALARMS_CHANGE, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trig, [(cg.uint8, "count")], conf)


# ── actions ────────────────────────────────────────────────────────────

SIMPLE_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(BiteKeeper)})


def _simple(name, cls):
    @automation.register_action(name, cls, SIMPLE_SCHEMA)
    async def _to_code(config, action_id, template_arg, args):
        var = cg.new_Pvariable(action_id, template_arg)
        await cg.register_parented(var, config[CONF_ID])
        return var

    return _to_code


_simple("bitekeeper.start", StartAction)
_simple("bitekeeper.stop", StopAction)
_simple("bitekeeper.poll", PollAction)
_simple("bitekeeper.forget", ForgetAction)
_simple("bitekeeper.scan_clear", ScanClearAction)


@automation.register_action(
    "bitekeeper.pair",
    PairAction,
    automation.maybe_simple_id(
        {
            cv.GenerateID(): cv.use_id(BiteKeeper),
            # Without mac: the strongest receiver found by the last scan.
            cv.Optional(CONF_MAC): cv.templatable(cv.string),
            cv.Optional(CONF_NAME): cv.templatable(cv.string),
        }
    ),
)
async def pair_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_MAC in config:
        cg.add(var.set_mac(await cg.templatable(config[CONF_MAC], args, cg.std_string)))
    if CONF_NAME in config:
        cg.add(
            var.set_name(await cg.templatable(config[CONF_NAME], args, cg.std_string))
        )
    return var


@automation.register_action(
    "bitekeeper.set_poll_interval",
    SetPollIntervalAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(BiteKeeper),
            cv.Required(CONF_INTERVAL): cv.templatable(
                cv.positive_time_period_milliseconds
            ),
        }
    ),
)
async def set_poll_interval_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    cg.add(
        var.set_interval(await cg.templatable(config[CONF_INTERVAL], args, cg.uint32))
    )
    return var


# ── settings ───────────────────────────────────────────────────────────


async def _set_fields(var, config, args, fields):
    for key, typ in fields:
        if key in config:
            cg.add(getattr(var, f"set_{key}")(await cg.templatable(config[key], args, typ)))


def _alarm_number():
    return cv.templatable(cv.int_range(min=1, max=8))


@automation.register_action(
    "bitekeeper.set_alarm",
    SetAlarmAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(BiteKeeper),
            cv.Required(CONF_ALARM): _alarm_number(),
            cv.Optional("volume"): cv.templatable(cv.int_range(min=0, max=9)),
            cv.Optional("tone"): cv.templatable(cv.int_range(min=0, max=8)),
            cv.Optional("sensitivity"): cv.templatable(cv.int_range(min=0, max=9)),
            cv.Optional("color"): cv.templatable(cv.int_range(min=0, max=5)),
            cv.Optional("mute"): cv.templatable(cv.boolean),
            cv.Optional("drop_back"): cv.templatable(cv.boolean),
        }
    ),
)
async def set_alarm_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    await _set_fields(var, config, args, [
        (CONF_ALARM, cg.uint8), ("volume", cg.uint8), ("tone", cg.uint8), ("sensitivity", cg.uint8),
        ("color", cg.uint8), ("mute", cg.bool_), ("drop_back", cg.bool_),
    ])
    return var


@automation.register_action(
    "bitekeeper.set_receiver",
    SetReceiverAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(BiteKeeper),
            cv.Optional("volume"): cv.templatable(cv.int_range(min=0, max=9)),
            cv.Optional("vibration"): cv.templatable(cv.boolean),
            cv.Optional("mute_all"): cv.templatable(cv.boolean),
        }
    ),
)
async def set_receiver_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    await _set_fields(var, config, args, [("volume", cg.uint8), ("vibration", cg.bool_), ("mute_all", cg.bool_)])
    return var


def _validate_drift(config):
    if CONF_ALARM in config:
        for key in ("speed", "distance"):
            if key not in config:
                raise cv.Invalid(f"'{key}' is required together with 'alarm'")
    elif "enabled" not in config:
        raise cv.Invalid("either 'enabled' (every alarm) or 'alarm' + 'speed' + 'distance'")
    return config


@automation.register_action(
    "bitekeeper.set_drift",
    SetDriftAction,
    cv.All(
        cv.Schema(
            {
                cv.GenerateID(): cv.use_id(BiteKeeper),
                cv.Optional(CONF_ALARM): _alarm_number(),
                cv.Optional("enabled"): cv.templatable(cv.boolean),
                # speed in 0.1 cm/s (10..40), distance in cm (5..20)
                cv.Optional("speed"): cv.templatable(cv.int_range(min=10, max=40)),
                cv.Optional("distance"): cv.templatable(cv.int_range(min=5, max=20)),
            }
        ),
        _validate_drift,
    ),
)
async def set_drift_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    await _set_fields(var, config, args, [
        (CONF_ALARM, cg.uint8), ("enabled", cg.bool_), ("speed", cg.uint8), ("distance", cg.uint8),
    ])
    return var


@automation.register_action(
    "bitekeeper.send_command",
    SendCommandAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(BiteKeeper),
            cv.Required("command"): cv.templatable(cv.uint8_t),
            # raw alarm byte: 0-based alarm index, 0xFF = all
            cv.Optional("alarm_index"): cv.templatable(cv.uint8_t),
            cv.Optional("param1"): cv.templatable(cv.uint8_t),
            cv.Optional("param2"): cv.templatable(cv.uint8_t),
            cv.Optional("param3"): cv.templatable(cv.uint8_t),
            cv.Optional("param4"): cv.templatable(cv.uint8_t),
        }
    ),
)
async def send_command_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    await _set_fields(var, config, args, [
        ("command", cg.uint8), ("alarm_index", cg.uint8), ("param1", cg.uint8),
        ("param2", cg.uint8), ("param3", cg.uint8), ("param4", cg.uint8),
    ])
    return var
