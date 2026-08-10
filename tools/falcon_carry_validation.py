#!/usr/bin/env python3
"""Live TCP evidence for Falcon's bounded native shell-carry bridge.

The runner is paused for every seed write.  It only fills one normal-sprite
slot with the stock green-shell ID and documented sprite/carry tables; stock
SMW owns status-$0B positioning, release, throw, and set-down thereafter.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
from typing import Any

import falcon_validation as fv


REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO / "build-falcon" / "SuperMarioWorldSNESRecomp.exe"
DEFAULT_ROM = REPO / "smw.sfc"
DEFAULT_SCENARIO = REPO / "test" / "falcon_validation" / "falcon_carry_seed.json"
SLOT_COUNT = 22


def fail(message: str) -> None:
    raise RuntimeError(message)


def require_u8(value: Any, field: str) -> int:
    parsed = fv.parse_int(value, field)
    if not 0 <= parsed <= 0xFF:
        raise fv.ScenarioError(f"{field} must fit in one byte")
    return parsed


def load_scenario(path: pathlib.Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise fv.ScenarioError(f"scenario does not exist: {path}") from exc
    except json.JSONDecodeError as exc:
        raise fv.ScenarioError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(data, dict) or data.get("format") != "falcon-carry-validation/v1":
        raise fv.ScenarioError("scenario.format must be 'falcon-carry-validation/v1'")
    mod = data.get("mod")
    if not isinstance(mod, dict):
        raise fv.ScenarioError("scenario.mod must be an object")
    for key in ("package_id", "feature_id", "version", "resource_id"):
        fv.safe_identifier(mod.get(key), f"scenario.mod.{key}")
    slot = fv.parse_int(data.get("slot"), "scenario.slot")
    if not 0 <= slot < SLOT_COUNT:
        raise fv.ScenarioError(f"scenario.slot must be below {SLOT_COUNT}")
    require_u8(data.get("sprite_id"), "scenario.sprite_id")
    return data


def write_ram(client: fv.TcpClient, address: int, data: bytes) -> dict[str, Any]:
    if not data or address < 0 or address + len(data) > 0x20000:
        fail("bounded write_ram request is outside WRAM")
    reply = client.command(f"write_ram {address:x} {data.hex()}")
    if reply.get("count") != len(data):
        fail(f"write_ram short write at 0x{address:05x}: {reply}")
    return reply


def read_u8(client: fv.TcpClient, address: int) -> int:
    data, _ = fv.read_wram(client, address, 1)
    return data[0]


def read_u16(client: fv.TcpClient, address: int) -> int:
    data, _ = fv.read_wram(client, address, 2)
    return int.from_bytes(data, "little")


def step(client: fv.TcpClient, frames: int) -> dict[str, Any]:
    reply = client.command(f"step {frames}")
    if reply.get("timeout"):
        fail(f"runner timed out advancing {frames} frames: {reply}")
    return reply


def set_input(client: fv.TcpClient, buttons: str) -> dict[str, Any]:
    return client.command(f"set_controller p1={buttons}")


def boot_to_gm14(client: fv.TcpClient) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    records.append(fv.wait_ram(client, {
        "id": "title_ready", "addr": "0x0100", "len": 1, "equals": "0x07",
        "timeout_frames": 1200, "step_frames": 4,
    }))
    step(client, 3)
    records.append(fv.wait_ram(client, {
        "id": "overworld_ready", "p1": ["start"], "addr": "0x0100", "len": 1,
        "equals": "0x0e", "timeout_frames": 1800, "hold_frames": 1,
        "release_frames": 8,
    }, pulse=True))
    step(client, 3)
    records.append(fv.wait_ram(client, {
        "id": "gameplay_ready", "p1": ["a"], "addr": "0x0100", "len": 1,
        "equals": "0x14", "timeout_frames": 1200, "hold_frames": 1,
        "release_frames": 8,
    }, pulse=True))
    step(client, 4)
    return records


def seed_shell(client: fv.TcpClient, slot: int, sprite_id: int, carried: bool) -> list[dict[str, Any]]:
    """Write a complete minimal normal-sprite shell state while paused.

    $009E/$14C8 are sprite number/status; $00E4/$14E0 and $00D8/$14D4 are
    position; $00AA/$00B6 are speed; $14EC/$14F8 are subposition; $1504,
    $151C, $1540, and $15EA are per-sprite working/OAM fields.  $1470/$148F
    are the stock player carry flags.  The status write is deliberately last.
    """
    px, py = read_u16(client, 0x0094), read_u16(client, 0x0096)
    shell_x = (px + 24) & 0xFFFF
    shell_y = (py - 16) & 0xFFFF
    writes = [
        (0x14C8 + slot, b"\x00"),
        (0x009E + slot, bytes([sprite_id])),
        (0x00E4 + slot, bytes([shell_x & 0xFF])),
        (0x14E0 + slot, bytes([shell_x >> 8])),
        (0x00D8 + slot, bytes([shell_y & 0xFF])),
        (0x14D4 + slot, bytes([shell_y >> 8])),
        (0x00AA + slot, b"\x00"), (0x00B6 + slot, b"\x00"),
        (0x14EC + slot, b"\x00"), (0x14F8 + slot, b"\x00"),
        (0x1504 + slot, b"\x00"), (0x151C + slot, b"\x00"),
        (0x1540 + slot, b"\x00"), (0x15EA + slot, b"\x00"),
        (0x1470, bytes([1 if carried else 0])),
        (0x148F, bytes([1 if carried else 0])),
        (0x14C8 + slot, bytes([0x0B if carried else 0x09])),
    ]
    return [{"addr": f"0x{address:05x}", "tcp": write_ram(client, address, data)}
            for address, data in writes]


def capture(client: fv.TcpClient, output: pathlib.Path, ident: str, slot: int,
            status: int | None = None) -> dict[str, Any]:
    regions = [
        {"name": "game_mode", "addr": "0x0100", "len": 1, "equals": "0x14"},
        {"name": "carry_flags", "addr": "0x1470", "len": 2},
        {"name": "sprite_number", "addr": f"0x{0x009E + slot:04x}", "len": 1,
         "equals": "0x04"},
        {"name": "sprite_status", "addr": f"0x{0x14C8 + slot:04x}", "len": 1},
        {"name": "sprite_yspeed", "addr": f"0x{0x00AA + slot:04x}", "len": 1},
        {"name": "sprite_xspeed", "addr": f"0x{0x00B6 + slot:04x}", "len": 1},
        {"name": "sprite_position", "addr": f"0x{0x00D8 + slot:04x}", "len": 1},
        {"name": "player", "addr": "0x0071", "len": 42},
    ]
    if status is not None:
        regions[3]["equals"] = f"0x{status:02x}"
    return fv.capture(client, {"id": ident, "wram": regions}, output)


def run(args: argparse.Namespace) -> int:
    scenario = load_scenario(args.scenario)
    if not args.exe.is_file():
        message = f"SKIP: Falcon executable not found: {args.exe}"
        print(message)
        return 2 if args.require_build else 0
    args.out.mkdir(parents=True, exist_ok=True)
    guard = fv.stage_mod_state(args.exe, scenario["mod"], args.owner_rom)
    process: subprocess.Popen[Any] | None = None
    client: fv.TcpClient | None = None
    evidence: dict[str, Any] = {"format": "falcon-carry-evidence/v1",
                                "scenario": args.scenario.name, "steps": []}
    slot = fv.parse_int(scenario["slot"], "scenario.slot")
    sprite_id = require_u8(scenario["sprite_id"], "scenario.sprite_id")
    try:
        process = subprocess.Popen(fv.launch_argv(args.exe, args.rom), cwd=str(args.exe.parent))
        client = fv.wait_for_server(process, args.port, args.timeout)
        evidence["boot"] = boot_to_gm14(client)

        evidence["steps"].append({"id": "shell_approach_seed", "writes":
                                  seed_shell(client, slot, sprite_id, False)})
        set_input(client, "none")
        step(client, 1)
        evidence["steps"].append(capture(client, args.out, "shell_approach", slot, 0x09))

        evidence["steps"].append({"id": "shell_carry_seed", "writes":
                                  seed_shell(client, slot, sprite_id, True)})
        set_input(client, "a")
        step(client, 2)
        evidence["steps"].append(capture(client, args.out, "shell_carried", slot, 0x0B))

        set_input(client, "none")
        step(client, 1)
        thrown = capture(client, args.out, "shell_thrown", slot, 0x0A)
        if read_u8(client, 0x00B6 + slot) == 0:
            fail("native throw left shell X speed at zero")
        evidence["steps"].append(thrown)

        evidence["steps"].append({"id": "shell_setdown_seed", "writes":
                                  seed_shell(client, slot, sprite_id, True)})
        set_input(client, "a")
        step(client, 1)
        set_input(client, "down")
        step(client, 1)
        evidence["steps"].append(capture(client, args.out, "shell_set_down", slot, 0x09))
        set_input(client, "none")

        (args.out / "evidence.json").write_text(
            json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"PASS: wrote {args.out / 'evidence.json'}")
        return 0
    finally:
        if client:
            client.close()
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
        if guard:
            guard.restore()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    parser.add_argument("--rom", type=pathlib.Path, default=DEFAULT_ROM)
    parser.add_argument("--owner-rom", type=pathlib.Path, required=True)
    parser.add_argument("--scenario", type=pathlib.Path, default=DEFAULT_SCENARIO)
    parser.add_argument("--out", type=pathlib.Path, default=REPO / "_triage" / "falcon_carry")
    parser.add_argument("--port", type=int, default=4377)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--require-build", action="store_true")
    args = parser.parse_args()
    try:
        return run(args)
    except (OSError, RuntimeError, fv.ScenarioError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
