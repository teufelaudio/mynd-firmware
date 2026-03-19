from __future__ import annotations

import asyncio
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType, SimpleNamespace
from unittest.mock import AsyncMock, patch


BIN_DIR = Path(__file__).resolve().parents[1] / "payload" / "bin"
if str(BIN_DIR) not in sys.path:
    sys.path.insert(0, str(BIN_DIR))


def install_actionslink_stubs():
    if "generated" in sys.modules:
        return

    class _EnumValue:
        def __init__(self, name: str):
            self.name = name

    class _EnumDescriptor:
        def __init__(self, mapping):
            self.values_by_number = {value: _EnumValue(name) for value, name in mapping.items()}

    class _Marker:
        def __init__(self):
            self.is_set = False

        def SetInParent(self):
            self.is_set = True

    class _Status:
        def __init__(self):
            self.code = 0

    class _ErrorMessage:
        def __init__(self):
            self.code = 0

    class _CommonResult:
        def __init__(self):
            self.status = _ErrorMessage()

    class _ResponseField:
        def __init__(self):
            self.status = _Status()

    class _WifiInfo:
        def __init__(self):
            self.ssid = ""
            self.ip_address = ""
            self.username = ""

    class _WifiCommandResult:
        def __init__(self):
            self.command_id = 0
            self.action_type = 0
            self.status = _CommonResult()
            self.target_reached = True
            self.detail = 0

    class _PowerStateEvent:
        def __init__(self):
            self.mode = 0

    class _LedPatternEvent:
        def __init__(self):
            self.pattern = 0

    class ToMcuResponse:
        def __init__(self):
            self.seq = 0
            self.set_power_state = _ResponseField()
            self.send_playback_action = _ResponseField()
            self.set_volume = _ResponseField()
            self.cycle_source = _ResponseField()

    class ToMcuEvent:
        def __init__(self):
            self.notify_system_ready = _Marker()
            self.notify_power_state = _PowerStateEvent()
            self.notify_host_source = 0
            self.notify_stream_state = False
            self.notify_play_led_pattern = _LedPatternEvent()
            self.notify_wifi_info = _WifiInfo()
            self.notify_wifi_command_result = _WifiCommandResult()

    class ToMcuRequest:
        def __init__(self):
            self.get_mcu_firmware_version = _Marker()

    generated = ModuleType("generated")
    generated.__path__ = []
    sys.modules["generated"] = generated

    audio_pb = ModuleType("audio_pb2")
    audio_pb.VolumeControl = SimpleNamespace(
        VolumeControlAction=SimpleNamespace(VOLUME_UP=1, VOLUME_DOWN=2)
    )

    battery_pb = ModuleType("battery_pb2")
    battery_pb.ChargerStatus = SimpleNamespace(Active=1)

    error_pb = ModuleType("error_pb2")
    error_pb.Code = SimpleNamespace(Success=0, OperationFailed=1, ResourceUnavailable=2)

    host_pb = ModuleType("host_pb2")
    host_pb.SOURCE_UNKNOWN = 0
    host_pb.SOURCE_MPD = 1
    host_pb.SOURCE_SPOTIFY = 2
    host_pb.SOURCE_AIRPLAY = 3
    host_pb.SOURCE_BLUETOOTH = 4
    host_pb.Source = SimpleNamespace(
        DESCRIPTOR=_EnumDescriptor(
            {
                0: "SOURCE_UNKNOWN",
                1: "SOURCE_MPD",
                2: "SOURCE_SPOTIFY",
                3: "SOURCE_AIRPLAY",
                4: "SOURCE_BLUETOOTH",
            }
        )
    )
    host_pb.PlaybackAction = SimpleNamespace(
        Action=SimpleNamespace(
            TOGGLE_PLAY_PAUSE=1,
            NEXT_TRACK=2,
            PREVIOUS_TRACK=3,
            DESCRIPTOR=_EnumDescriptor(
                {
                    1: "TOGGLE_PLAY_PAUSE",
                    2: "NEXT_TRACK",
                    3: "PREVIOUS_TRACK",
                }
            ),
        )
    )
    host_pb.WiFiCommandResult = SimpleNamespace(
        ActionType=SimpleNamespace(
            ACTION_TYPE_UNKNOWN=0,
            ACTION_TYPE_CONFIGURE_WIFI=1,
            ACTION_TYPE_ENABLE_HOTSPOT=2,
            ACTION_TYPE_CYCLE_WIFI_NETWORK=3,
        ),
        Detail=SimpleNamespace(
            DETAIL_NONE=0,
            DETAIL_BUSY=1,
        ),
    )

    leds_pb = ModuleType("leds_pb2")
    leds_pb.PlayLedPattern = SimpleNamespace(
        Pattern=SimpleNamespace(POSITIVE_FEEDBACK=1, NEGATIVE_FEEDBACK=2)
    )

    message_pb = ModuleType("message_pb2")
    message_pb.ToMcuResponse = ToMcuResponse
    message_pb.ToMcuEvent = ToMcuEvent
    message_pb.ToMcuRequest = ToMcuRequest

    system_pb = ModuleType("system_pb2")
    system_pb.PowerState = SimpleNamespace(
        SystemPowerMode=SimpleNamespace(
            OFF=0,
            ON=1,
            STANDBY=2,
            SHUTDOWN_REQUEST=3,
            DESCRIPTOR=_EnumDescriptor(
                {
                    0: "OFF",
                    1: "ON",
                    2: "STANDBY",
                    3: "SHUTDOWN_REQUEST",
                }
            ),
        )
    )

    actionslink_client = ModuleType("actionslink_client")
    actionslink_client.ActionsLinkClient = FakeActionsLinkClientForImport

    module_map = {
        "actionslink_client": actionslink_client,
        "audio_pb2": audio_pb,
        "battery_pb2": battery_pb,
        "error_pb2": error_pb,
        "host_pb2": host_pb,
        "leds_pb2": leds_pb,
        "message_pb2": message_pb,
        "system_pb2": system_pb,
        "generated.audio_pb2": audio_pb,
        "generated.battery_pb2": battery_pb,
        "generated.error_pb2": error_pb,
        "generated.host_pb2": host_pb,
        "generated.leds_pb2": leds_pb,
        "generated.message_pb2": message_pb,
        "generated.system_pb2": system_pb,
    }
    for name, module in module_map.items():
        sys.modules[name] = module


class FakeActionsLinkClientForImport:
    def __init__(self, _device):
        self.is_connected = True

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        return False

    async def send_event(self, *_args, **_kwargs):
        return True

    async def send_request(self, *_args, **_kwargs):
        return SimpleNamespace()

    async def send_response(self, *_args, **_kwargs):
        return True

    def on_event(self, _handler):
        return None

    def on_request(self, _name, _handler):
        return None


def install_aiohttp_stub():
    if "aiohttp" in sys.modules:
        return

    class ClientTimeout:
        def __init__(self, total=None):
            self.total = total

    class TCPConnector:
        def __init__(self, ssl=False):
            self.ssl = ssl

    class ClientSession:
        def __init__(self, *args, **kwargs):
            self.args = args
            self.kwargs = kwargs

        async def close(self):
            return None

    aiohttp = ModuleType("aiohttp")
    aiohttp.ClientTimeout = ClientTimeout
    aiohttp.TCPConnector = TCPConnector
    aiohttp.ClientSession = ClientSession
    sys.modules["aiohttp"] = aiohttp


install_actionslink_stubs()
install_aiohttp_stub()

import bluetooth_controller
import mynd_rpi_link
from actionslink_adapter import error_pb, host_pb, message_pb
from daemon_context import MyndRpiDaemon, POWER_STATE_OFF, POWER_STATE_ON
from power_controller import PowerController
from request_handlers import RequestHandlers
from wifi_controller import WifiController


class FakeClient:
    def __init__(self):
        self.is_connected = True
        self.responses = []
        self.events = []
        self.request_handlers = {}
        self.event_handler = None

    async def send_response(self, response):
        self.responses.append(response)
        return True

    async def send_event(self, event, timeout=None):
        self.events.append((event, timeout))
        return True

    async def send_request(self, request, expected_response=None, timeout=None):
        self.last_request = (request, expected_response, timeout)
        return SimpleNamespace()

    def on_event(self, handler):
        self.event_handler = handler

    def on_request(self, name, handler):
        self.request_handlers[name] = handler


class FakeActionsLinkClient(FakeClient):
    def __init__(self, _device):
        super().__init__()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        return False


class FakeMoodeClient:
    def __init__(self, *_args, **_kwargs):
        self.started = False

    async def start(self):
        self.started = True

    async def stop(self):
        self.started = False

    async def refresh_volume_settings(self):
        return None


class FakeBluetoothController:
    def __init__(self, _logger, _config):
        self.playing_signal_state = None

    def start_listener(self):
        return None

    def stop_listener(self):
        return None


class FakeWifiController:
    def __init__(self, _daemon):
        self.restore_radio_after_stop = AsyncMock()


class FakePlaybackController:
    def __init__(self, daemon, _bluetooth, _wifi):
        self.daemon = daemon
        self.start_monitor = AsyncMock()
        self.stop_monitor = AsyncMock()


class FakePowerController:
    def __init__(self, daemon, _playback, _wifi):
        self.daemon = daemon
        self.ensure_startup_power_on = AsyncMock(side_effect=self._ensure_startup_power_on)

    async def _ensure_startup_power_on(self, _client, timeout_s=5.0):
        assert self.daemon.running is True
        self.daemon.running = False


def make_daemon() -> MyndRpiDaemon:
    with tempfile.NamedTemporaryFile("w", delete=False) as config_file:
        config_file.write("[logging]\nuse_syslog=false\nlevel=DEBUG\n")
        config_path = config_file.name
    return MyndRpiDaemon(config_path)


class RunDaemonTests(unittest.IsolatedAsyncioTestCase):
    async def test_run_daemon_sets_running_before_startup_power_on(self):
        daemon = make_daemon()
        daemon.validate_serial_device = lambda _device: (True, "/dev/null", "")

        with (
            patch.object(mynd_rpi_link, "ActionsLinkClient", FakeActionsLinkClient),
            patch.object(mynd_rpi_link, "MoodeClient", FakeMoodeClient),
            patch.object(mynd_rpi_link, "BluetoothController", FakeBluetoothController),
            patch.object(mynd_rpi_link, "WifiController", FakeWifiController),
            patch.object(mynd_rpi_link, "PlaybackController", FakePlaybackController),
            patch.object(mynd_rpi_link, "PowerController", FakePowerController),
        ):
            result = await mynd_rpi_link.run_daemon(daemon)

        self.assertEqual(result, 0)


class BluetoothControllerTests(unittest.IsolatedAsyncioTestCase):
    async def test_send_bt_volume_press_uses_bluealsa_cli_step(self):
        logger = SimpleNamespace(
            info=lambda *args, **kwargs: None,
            warning=lambda *args, **kwargs: None,
            debug=lambda *args, **kwargs: None,
            error=lambda *args, **kwargs: None,
        )
        controller = bluetooth_controller.BluetoothController(
            logger,
            {"configure_moode": {"bt_sw_volume_step": 2}},
        )
        pcm_path = "/org/bluealsa/hci0/dev_FC_5B_8C_F7_5D_FB/a2dpsnk/source"

        with patch(
            "bluetooth_controller.subprocess.run",
            side_effect=[
                SimpleNamespace(returncode=0, stdout=f"{pcm_path}\n", stderr=""),
                SimpleNamespace(returncode=0, stdout="Volume: L: 7 R: 7", stderr=""),
                SimpleNamespace(returncode=0, stdout="", stderr=""),
            ],
        ) as run_mock:
            success = await controller.send_bt_volume_press(True)

        self.assertTrue(success)
        self.assertIsNone(controller.last_volume_boundary)
        self.assertEqual(run_mock.call_args_list[0].args[0], ["bluealsa-cli", "list-pcms"])
        self.assertEqual(run_mock.call_args_list[1].args[0], ["bluealsa-cli", "volume", pcm_path])
        self.assertEqual(
            run_mock.call_args_list[2].args[0],
            ["bluealsa-cli", "volume", pcm_path, "9", "9"],
        )

    def test_parse_bluealsa_volume(self):
        self.assertEqual(
            bluetooth_controller._parse_bluealsa_volume("Volume: L: 7 R: 7"),
            (7, 7),
        )


class RequestHandlerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.daemon = make_daemon()
        self.client = FakeClient()
        self.daemon.playback = SimpleNamespace(
            handle_volume_action=AsyncMock(),
            handle_play_pause_for_active_source=AsyncMock(return_value=False),
            handle_next_track_for_active_source=AsyncMock(return_value=True),
            handle_previous_track_for_active_source=AsyncMock(return_value=True),
            begin_return_to_mpd=AsyncMock(return_value=(True, True, host_pb.SOURCE_AIRPLAY)),
            complete_return_to_mpd=AsyncMock(return_value=True),
        )
        self.daemon.wifi = SimpleNamespace(
            configure_wifi=AsyncMock(),
            enable_hotspot=AsyncMock(),
            cycle_wifi_network=AsyncMock(),
            begin_command=AsyncMock(return_value="started"),
            finish_command=AsyncMock(),
        )
        self.daemon.power = SimpleNamespace(
            pause_moode_playback=AsyncMock(return_value=True),
            handle_power_state=AsyncMock(return_value=None),
        )
        self.handlers = RequestHandlers(self.client, self.daemon)

    async def test_configure_wifi_success_event_emits_result_and_wifi_info(self):
        self.daemon.wifi.configure_wifi.return_value = SimpleNamespace(
            success=True,
            status_code=error_pb.Code.Success,
            ssid="TestWifi",
            ip_address="192.168.1.2",
            username="pi",
            target_reached=True,
            detail=host_pb.WiFiCommandResult.Detail.DETAIL_NONE,
        )

        await self.handlers.handle_configure_wifi_event(
            SimpleNamespace(
                command_id=7,
                config=SimpleNamespace(ssid="TestWifi", password="secret"),
            )
        )

        self.assertEqual(len(self.client.events), 2)
        result_event = self.client.events[0][0].notify_wifi_command_result
        wifi_info_event = self.client.events[1][0].notify_wifi_info
        self.assertEqual(result_event.command_id, 7)
        self.assertEqual(
            result_event.action_type,
            host_pb.WiFiCommandResult.ActionType.ACTION_TYPE_CONFIGURE_WIFI,
        )
        self.assertEqual(result_event.status.status.code, error_pb.Code.Success)
        self.assertEqual(wifi_info_event.ssid, "TestWifi")
        self.daemon.wifi.finish_command.assert_awaited_once_with(7)

    async def test_configure_wifi_failure_emits_result_only(self):
        self.daemon.wifi.configure_wifi.return_value = SimpleNamespace(
            success=False,
            status_code=error_pb.Code.OperationFailed,
            ssid="",
            ip_address="unknown",
            username="unknown",
            target_reached=False,
            detail=host_pb.WiFiCommandResult.Detail.DETAIL_NONE,
        )

        await self.handlers.handle_configure_wifi_event(
            SimpleNamespace(
                command_id=8,
                config=SimpleNamespace(ssid="BadWifi", password="bad"),
            )
        )

        self.assertEqual(len(self.client.events), 1)
        result_event = self.client.events[0][0].notify_wifi_command_result
        self.assertEqual(result_event.command_id, 8)
        self.assertEqual(result_event.status.status.code, error_pb.Code.OperationFailed)

    async def test_enable_hotspot_busy_emits_busy_result(self):
        self.daemon.wifi.begin_command.return_value = "busy"

        await self.handlers.handle_enable_hotspot_event(SimpleNamespace(command_id=11))

        self.assertEqual(len(self.client.events), 1)
        result_event = self.client.events[0][0].notify_wifi_command_result
        self.assertEqual(result_event.command_id, 11)
        self.assertEqual(result_event.status.status.code, error_pb.Code.ResourceUnavailable)
        self.assertEqual(result_event.detail, host_pb.WiFiCommandResult.Detail.DETAIL_BUSY)
        self.daemon.wifi.finish_command.assert_not_awaited()

    async def test_cycle_wifi_duplicate_command_is_suppressed(self):
        self.daemon.wifi.begin_command.return_value = "duplicate"

        await self.handlers.handle_cycle_wifi_network_event(SimpleNamespace(command_id=12))

        self.assertEqual(self.client.events, [])
        self.daemon.wifi.cycle_wifi_network.assert_not_awaited()
        self.daemon.wifi.finish_command.assert_not_awaited()

    async def test_wifi_event_ingress_schedules_command_handler(self):
        with patch.object(self.handlers, "schedule") as schedule_mock:
            schedule_mock.side_effect = lambda coro, _label: coro.close()
            event = SimpleNamespace(
                WhichOneof=lambda _name: "notify_configure_wifi_command",
                notify_configure_wifi_command=SimpleNamespace(
                    command_id=21,
                    config=SimpleNamespace(ssid="Guest", password="pw"),
                ),
            )

            self.handlers.handle_mcu_event(event)

        schedule_mock.assert_called_once()

    async def test_playback_action_reports_actual_failure(self):
        await self.handlers.handle_send_playback_action(
            9,
            SimpleNamespace(action=host_pb.PlaybackAction.Action.TOGGLE_PLAY_PAUSE),
        )

        self.assertEqual(len(self.client.responses), 1)
        self.assertEqual(
            self.client.responses[0].send_playback_action.status.code,
            error_pb.Code.OperationFailed,
        )

    async def test_cycle_source_pauses_moode_before_switching_to_mpd(self):
        call_order = []
        self.daemon.power.pause_moode_playback.side_effect = lambda: call_order.append("pause")
        self.daemon.playback.begin_return_to_mpd.side_effect = (
            lambda: call_order.append("begin") or (True, True, host_pb.SOURCE_AIRPLAY)
        )

        async def complete_return_to_mpd(_previous_source):
            call_order.append(f"complete_after_{len(self.client.responses)}_responses")
            return True

        self.daemon.playback.complete_return_to_mpd.side_effect = complete_return_to_mpd

        await self.handlers.handle_return_to_mpd(10, SimpleNamespace())

        self.daemon.power.pause_moode_playback.assert_awaited_once()
        self.daemon.playback.begin_return_to_mpd.assert_awaited_once()
        self.daemon.playback.complete_return_to_mpd.assert_awaited_once_with(host_pb.SOURCE_AIRPLAY)
        self.assertEqual(call_order, ["pause", "begin", "complete_after_1_responses"])
        self.assertEqual(len(self.client.responses), 1)
        self.assertEqual(
            self.client.responses[0].cycle_source.status.code,
            error_pb.Code.Success,
        )


class StartupFallbackTests(unittest.IsolatedAsyncioTestCase):
    async def test_initialize_daemon_services_restores_wifi_radio(self):
        daemon = make_daemon()
        daemon.wifi = SimpleNamespace(
            set_wifi_radio_state=AsyncMock(return_value=True),
            bt_wifi_radio_forced_off=True,
            bt_wifi_restore_connections=["HomeWifi"],
        )
        playback = SimpleNamespace(start_monitor=AsyncMock(), stop_monitor=AsyncMock())
        controller = PowerController(daemon, playback, daemon.wifi)
        client = FakeClient()

        await controller.initialize_daemon_services(client)

        daemon.wifi.set_wifi_radio_state.assert_awaited_once_with(True)
        self.assertFalse(daemon.wifi.bt_wifi_radio_forced_off)
        self.assertEqual(daemon.wifi.bt_wifi_restore_connections, [])
        self.assertTrue(daemon.power_state.initialized)
        playback.start_monitor.assert_awaited_once_with(client)

    async def test_startup_fallback_performs_synthetic_power_on(self):
        daemon = make_daemon()
        daemon.power_state.power_state_event = asyncio.Event()
        daemon.playback = SimpleNamespace(mpd=SimpleNamespace(query_state=AsyncMock(return_value="stop")))
        daemon.wifi = SimpleNamespace(set_wifi_radio_state=AsyncMock(return_value=True))
        controller = PowerController(daemon, SimpleNamespace(start_monitor=AsyncMock(), stop_monitor=AsyncMock()), daemon.wifi)
        daemon.power = controller
        client = FakeClient()
        client.send_request = AsyncMock(return_value=SimpleNamespace())

        with patch("power_controller.send_power_state", AsyncMock(return_value=True)) as send_power_state_mock:
            with patch.object(controller, "handle_power_state", AsyncMock(return_value=None)) as handle_power_state_mock:
                await controller.ensure_startup_power_on(client, timeout_s=0.01)

        self.assertTrue(daemon.power_state.power_state_event.is_set())
        self.assertEqual(daemon.power_state.rpi_power_state, POWER_STATE_ON)
        send_power_state_mock.assert_awaited_once()
        handle_power_state_mock.assert_awaited_once_with(client, POWER_STATE_ON)


class WifiControllerTests(unittest.IsolatedAsyncioTestCase):
    async def test_enable_hotspot_fails_when_moode_profile_requires_missing_secret(self):
        daemon = make_daemon()
        daemon.command_runner = SimpleNamespace(
            run=AsyncMock(
                side_effect=[
                    SimpleNamespace(returncode=0, stdout="", stderr=""),
                    SimpleNamespace(returncode=0, stdout="", stderr=""),
                    SimpleNamespace(
                        returncode=10,
                        stdout="",
                        stderr="Secrets were required, but not provided",
                    ),
                ]
            )
        )
        controller = WifiController(daemon)

        with patch("wifi_controller.socket.gethostname", return_value="MYND-moOde-RPi"):
            result = await controller.enable_hotspot()

        self.assertFalse(result.success)
        self.assertEqual(result.status_code, error_pb.Code.ResourceUnavailable)

        commands = [call.args[0] for call in daemon.command_runner.run.await_args_list]
        self.assertEqual(commands[0], ["sudo", "nmcli", "radio", "wifi", "on"])
        self.assertEqual(commands[1], ["nmcli", "connection", "show", "MYND-moOde-RPi"])
        self.assertEqual(
            commands[2],
            ["sudo", "nmcli", "connection", "up", "MYND-moOde-RPi", "ifname", "wlan0"],
        )


if __name__ == "__main__":
    unittest.main()
