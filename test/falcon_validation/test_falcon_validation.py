"""No-build unit tests for tools/falcon_validation.py."""
from __future__ import annotations

import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest


REPO = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("falcon_validation", REPO / "tools" / "falcon_validation.py")
assert SPEC and SPEC.loader
falcon = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = falcon
SPEC.loader.exec_module(falcon)


class FalconValidationTests(unittest.TestCase):
    def test_normalize_buttons(self) -> None:
        self.assertEqual(falcon.normalize_buttons(["right", "b"], "p1"), "right+b")
        self.assertEqual(falcon.normalize_buttons(None, "p1"), "none")
        self.assertEqual(falcon.normalize_buttons("0x180", "p1"), "0x180")

    def test_valid_fixture_loads(self) -> None:
        scenario = falcon.load_scenario(REPO / "test" / "falcon_validation" / "falcon_smoke.json")
        self.assertEqual(scenario["format"], "falcon-validation/v1")
        self.assertEqual(scenario["mod"]["feature_id"], "captain-falcon")
        self.assertEqual(scenario["steps"][0]["op"], "wait_ram")
        self.assertTrue(any(step["op"] == "pulse_input_until_ram" for step in scenario["steps"]))

    def test_rejects_out_of_range_wram(self) -> None:
        bad = {"format": "falcon-validation/v1", "steps": [
            {"op": "capture", "id": "bad", "wram": [
                {"name": "bad", "addr": "0x1ffff", "len": 2}
            ]}
        ]}
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "bad.json"
            path.write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaises(falcon.ScenarioError):
                falcon.load_scenario(path)

    def test_missing_build_skips_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            args = type("Args", (), {
                "scenario": REPO / "test" / "falcon_validation" / "falcon_smoke.json",
                "exe": root / "missing.exe", "out": root / "out", "port": 49999,
                "rom": root / "also-missing.sfc", "timeout": 0.1, "require_build": False,
            })()
            self.assertEqual(falcon.run(args), 0)

    def test_launch_argv_requires_an_existing_rom_and_makes_it_absolute(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            exe = root / "Falcon.exe"
            rom = root / "smw.sfc"
            rom.write_bytes(b"test-rom")
            self.assertEqual(
                falcon.launch_argv(exe, rom),
                [str(exe), "--paused", str(rom.resolve())],
            )
            with self.assertRaisesRegex(RuntimeError, "SMW ROM not found"):
                falcon.launch_argv(exe, root / "missing.sfc")

    def test_capture_uses_absolute_posix_screenshot_path(self) -> None:
        class Client:
            def __init__(self) -> None:
                self.commands: list[str] = []

            def command(self, command: str) -> dict[str, object]:
                self.commands.append(command)
                if command.startswith("screenshot "):
                    pathlib.Path(command.split(" ", 1)[1]).write_bytes(b"bmp")
                    return {"ok": True}
                if command == "frame":
                    return {"frame": 17}
                if command.startswith("read_ram "):
                    return {"hex": "00"}
                raise AssertionError(command)

        with tempfile.TemporaryDirectory() as temp:
            output = pathlib.Path(temp) / "tcp-baseline"
            output.mkdir()
            client = Client()
            falcon.capture(client, {"id": "boot", "wram": []}, output)
            screenshot = next(command for command in client.commands if command.startswith("screenshot "))
            self.assertEqual(screenshot, f"screenshot {falcon.tcp_file_path(output / 'boot.bmp')}")
            self.assertNotIn("\\", screenshot)

    def test_tcp_file_path_uses_a_windows_drive_with_forward_slashes(self) -> None:
        self.assertEqual(falcon.tcp_file_path(pathlib.Path("/f/triage/shot.bmp")), "F:/triage/shot.bmp")

    def test_wait_ram_advances_declared_frame_quanta(self) -> None:
        class Client:
            def __init__(self) -> None:
                self.frame = 100
                self.reads = ["00", "05"]
                self.commands: list[str] = []

            def command(self, command: str) -> dict[str, object]:
                self.commands.append(command)
                if command == "frame":
                    return {"frame": self.frame}
                if command.startswith("read_ram "):
                    return {"hex": self.reads.pop(0)}
                if command.startswith("step "):
                    self.frame += int(command.split()[1])
                    return {"ok": True, "stepped": int(command.split()[1])}
                raise AssertionError(command)

        client = Client()
        evidence = falcon.wait_ram(client, {
            "id": "ready", "addr": "0x100", "len": 1, "equals": "0x05",
            "timeout_frames": 30, "step_frames": 4,
        })
        self.assertEqual(evidence["frames_elapsed"], 4)
        self.assertEqual(evidence["observed"], "05")
        self.assertEqual(client.commands, ["frame", "read_ram 100 1", "frame", "step 4", "read_ram 100 1", "frame"])

    def test_mod_state_is_temporary_and_restored(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            exe = root / "build" / "SuperMarioWorldSNESRecomp.exe"
            exe.parent.mkdir()
            exe.write_bytes(b"exe")
            manifest = exe.parent / "mods" / "packages" / "example.falcon" / "1.0.0" / "manifest.toml"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("format_version = 1\n", encoding="utf-8")
            state = exe.parent / "mods" / "state.toml"
            state.write_bytes(b"previous-state\n")
            guard = falcon.stage_mod_state(exe, {
                "package_id": "example.falcon", "feature_id": "captain-falcon", "version": "1.0.0",
            }, None)
            assert guard is not None
            staged = state.read_text(encoding="utf-8")
            self.assertIn('enabled = true', staged)
            self.assertIn('id = "captain-falcon"', staged)
            guard.restore()
            self.assertEqual(state.read_bytes(), b"previous-state\n")

    def test_resource_state_requires_and_uses_explicit_owner_rom(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            exe = root / "build" / "Falcon.exe"
            exe.parent.mkdir()
            exe.write_bytes(b"exe")
            manifest = exe.parent / "mods" / "packages" / "example.falcon" / "1.0.0" / "manifest.toml"
            manifest.parent.mkdir(parents=True)
            manifest.write_text("format_version = 1\n", encoding="utf-8")
            mod = {"package_id": "example.falcon", "feature_id": "captain-falcon",
                   "version": "1.0.0", "resource_id": "smash64-us-v10"}
            with self.assertRaisesRegex(RuntimeError, "--owner-rom"):
                falcon.stage_mod_state(exe, mod, None)
            owner_rom = root / "owner.z64"
            owner_rom.write_bytes(b"owner")
            guard = falcon.stage_mod_state(exe, mod, owner_rom)
            assert guard is not None
            staged = guard.path.read_text(encoding="utf-8")
            self.assertIn('id = "smash64-us-v10"', staged)
            self.assertIn(owner_rom.resolve().as_posix(), staged)
            guard.restore()
            self.assertFalse(guard.path.exists())


if __name__ == "__main__":
    unittest.main()
