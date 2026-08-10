#!/usr/bin/env python3
"""Deterministic TCP evidence driver for the Captain Falcon SMW mod.

The driver has no build-time dependency.  It launches a TRACE Falcon build in
its existing start-paused mode, applies a declarative controller timeline, and
writes a manifest containing the exact response, screenshot SHA-256, and WRAM
slice SHA-256 at each checkpoint.  If no executable exists it reports a clean
SKIP (unless --require-build was supplied), which makes it usable before the
runtime integration lands.

The protocol is intentionally the SNES runner's line-oriented TCP protocol,
not a new side channel.  See docs/falcon_source_host_seam_map.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import socket
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import Any


REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO / "build-falcon" / "SuperMarioWorldSNESRecomp.exe"
DEFAULT_ROM = REPO / "smw.sfc"
DEFAULT_SCENARIO = REPO / "test" / "falcon_validation" / "falcon_smoke.json"


class ScenarioError(ValueError):
    """The scenario is syntactically valid JSON but not a valid test plan."""


def parse_int(value: Any, field: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise ScenarioError(f"{field} must be an integer or 0x-prefixed integer") from exc
    raise ScenarioError(f"{field} must be an integer or string")


def normalize_buttons(value: Any, field: str) -> str:
    """Render the server's accepted button format deterministically."""
    if value is None:
        return "none"
    if isinstance(value, str):
        return value
    if not isinstance(value, list) or not value or not all(isinstance(v, str) and v for v in value):
        raise ScenarioError(f"{field} must be a non-empty string list, a mask, or null")
    return "+".join(value)


def safe_identifier(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-" for c in value):
        raise ScenarioError(f"{field} must use only letters, numbers, ., _ and -")
    return value


def validate_ram_wait(step: dict[str, Any], index: int) -> None:
    field = f"steps[{index}]"
    address = parse_int(step.get("addr"), f"{field}.addr")
    length = parse_int(step.get("len", 1), f"{field}.len")
    expected = parse_int(step.get("equals"), f"{field}.equals")
    timeout_frames = parse_int(step.get("timeout_frames"), f"{field}.timeout_frames")
    step_frames = parse_int(step.get("step_frames", 1), f"{field}.step_frames")
    if address < 0 or length < 1 or address + length > 0x20000:
        raise ScenarioError(f"{field} is outside 128 KiB WRAM")
    if expected < 0 or expected >= (1 << (length * 8)):
        raise ScenarioError(f"{field}.equals does not fit in len bytes")
    if timeout_frames < 1 or step_frames < 1:
        raise ScenarioError(f"{field} timeout_frames and step_frames must be positive")


def load_scenario(path: pathlib.Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ScenarioError(f"scenario does not exist: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ScenarioError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(data, dict) or data.get("format") != "falcon-validation/v1":
        raise ScenarioError("scenario.format must be 'falcon-validation/v1'")
    mod = data.get("mod")
    if mod is not None:
        if not isinstance(mod, dict):
            raise ScenarioError("scenario.mod must be an object")
        safe_identifier(mod.get("package_id"), "scenario.mod.package_id")
        safe_identifier(mod.get("feature_id"), "scenario.mod.feature_id")
        safe_identifier(mod.get("version"), "scenario.mod.version")
        if "resource_id" in mod:
            safe_identifier(mod["resource_id"], "scenario.mod.resource_id")
    steps = data.get("steps")
    if not isinstance(steps, list) or not steps:
        raise ScenarioError("scenario.steps must be a non-empty array")
    for index, step in enumerate(steps):
        if not isinstance(step, dict) or not isinstance(step.get("op"), str):
            raise ScenarioError(f"steps[{index}] must contain string op")
        op = step["op"]
        if op == "input":
            normalize_buttons(step.get("p1"), f"steps[{index}].p1")
            if "p2" in step:
                normalize_buttons(step["p2"], f"steps[{index}].p2")
        elif op == "step":
            if parse_int(step.get("frames"), f"steps[{index}].frames") < 1:
                raise ScenarioError(f"steps[{index}].frames must be positive")
        elif op == "wait_ram":
            validate_ram_wait(step, index)
        elif op == "pulse_input_until_ram":
            normalize_buttons(step.get("p1"), f"steps[{index}].p1")
            if "p2" in step:
                normalize_buttons(step["p2"], f"steps[{index}].p2")
            validate_ram_wait(step, index)
            hold_frames = parse_int(step.get("hold_frames", 1), f"steps[{index}].hold_frames")
            release_frames = parse_int(step.get("release_frames", 1), f"steps[{index}].release_frames")
            if hold_frames < 1 or release_frames < 1:
                raise ScenarioError(f"steps[{index}] hold_frames and release_frames must be positive")
        elif op == "capture":
            ident = step.get("id")
            if not isinstance(ident, str) or not ident or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in ident):
                raise ScenarioError(f"steps[{index}].id must use only letters, numbers, _ and -")
            regions = step.get("wram", [])
            if not isinstance(regions, list):
                raise ScenarioError(f"steps[{index}].wram must be an array")
            for region in regions:
                if not isinstance(region, dict) or not isinstance(region.get("name"), str):
                    raise ScenarioError(f"steps[{index}].wram entries need name, addr, len")
                address = parse_int(region.get("addr"), f"{region['name']}.addr")
                length = parse_int(region.get("len"), f"{region['name']}.len")
                if address < 0 or length < 1 or address + length > 0x20000:
                    raise ScenarioError(f"{region['name']} is outside 128 KiB WRAM")
        else:
            raise ScenarioError(f"steps[{index}].op must be input, step, wait_ram, pulse_input_until_ram, or capture")
    return data


@dataclass
class TcpClient:
    host: str
    port: int
    timeout: float
    sock: socket.socket | None = None
    file: Any = None

    def connect(self) -> None:
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self.sock.settimeout(self.timeout)
        self.file = self.sock.makefile("rb")

    def command(self, line: str) -> dict[str, Any]:
        if not self.sock or not self.file:
            raise RuntimeError("TCP client is not connected")
        self.sock.sendall((line + "\n").encode("ascii"))
        raw = self.file.readline()
        if not raw:
            raise RuntimeError(f"server closed the connection after: {line}")
        try:
            reply = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"non-JSON response to {line!r}: {raw[:200]!r}") from exc
        if not isinstance(reply, dict):
            raise RuntimeError(f"non-object response to {line!r}: {reply!r}")
        if reply.get("ok") is False or "error" in reply:
            raise RuntimeError(f"server rejected {line!r}: {reply}")
        return reply

    def close(self) -> None:
        if self.file:
            self.file.close()
        if self.sock:
            self.sock.close()
        self.file = None
        self.sock = None


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tcp_file_path(path: pathlib.Path) -> str:
    """Return an absolute, slash-only path understood by the Windows runner."""
    value = str(path.resolve()).replace("\\", "/")
    # MSYS Python renders F:\\... as /f/...; the native runner's fopen needs
    # a drive-qualified path, while its command parser needs slash separators.
    if len(value) >= 3 and value[0] == "/" and value[1].isalpha() and value[2] == "/":
        value = value[1].upper() + ":" + value[2:]
    return value


def read_wram(client: TcpClient, address: int, length: int) -> tuple[bytes, dict[str, Any]]:
    reply = client.command(f"read_ram {address:x} {length}")
    payload = reply.get("hex")
    if not isinstance(payload, str):
        raise RuntimeError(f"read_ram response lacks hex payload: {reply}")
    try:
        data = bytes.fromhex(payload)
    except ValueError as exc:
        raise RuntimeError(f"read_ram returned malformed hex: {reply}") from exc
    if len(data) != length:
        raise RuntimeError(f"read_ram returned {len(data)} bytes, wanted {length}")
    return data, reply


def wait_for_server(process: subprocess.Popen[Any], port: int, timeout: float) -> TcpClient:
    deadline = time.monotonic() + timeout
    last_error: OSError | None = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Falcon process exited before TCP was ready ({process.returncode})")
        try:
            client = TcpClient("127.0.0.1", port, timeout)
            client.connect()
            return client
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    raise RuntimeError(f"TCP server 127.0.0.1:{port} did not start within {timeout}s: {last_error}")


def launch_argv(exe: pathlib.Path, rom: pathlib.Path) -> list[str]:
    """Return the game's established `--paused <absolute-rom>` invocation."""
    if not rom.is_file():
        raise RuntimeError(f"SMW ROM not found: {rom}")
    return [str(exe.resolve()), "--paused", str(rom.resolve())]


@dataclass
class ModStateGuard:
    path: pathlib.Path
    original: bytes | None

    def restore(self) -> None:
        if self.original is None:
            self.path.unlink(missing_ok=True)
        else:
            atomic_write(self.path, self.original)


def atomic_write(path: pathlib.Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".falcon-validation.tmp")
    temp.write_bytes(data)
    os.replace(temp, path)


def stage_mod_state(exe: pathlib.Path, mod: dict[str, Any] | None,
                    owner_rom: pathlib.Path | None) -> ModStateGuard | None:
    """Temporarily select the scenario's trusted feature before process launch."""
    if mod is None:
        return None
    package_id = safe_identifier(mod["package_id"], "scenario.mod.package_id")
    feature_id = safe_identifier(mod["feature_id"], "scenario.mod.feature_id")
    version = safe_identifier(mod["version"], "scenario.mod.version")
    resource_id = (safe_identifier(mod["resource_id"], "scenario.mod.resource_id")
                   if "resource_id" in mod else None)
    mods = exe.parent / "mods"
    manifest = mods / "packages" / package_id / version / "manifest.toml"
    if not manifest.is_file():
        raise RuntimeError(f"requested Falcon mod package is not staged beside executable: {manifest}")
    state = mods / "state.toml"
    original = state.read_bytes() if state.is_file() else None
    if resource_id and (owner_rom is None or not owner_rom.is_file()):
        raise RuntimeError(f"scenario requests owner resource {resource_id!r}; pass an existing --owner-rom")
    payload = (
        "format_version = 1\n\n"
        "[[package]]\n"
        f'id = "{package_id}"\n'
        f'version = "{version}"\n\n'
        "[[feature]]\n"
        f'package_id = "{package_id}"\n'
        f'id = "{feature_id}"\n'
        "enabled = true\n"
    )
    if resource_id:
        payload += (
            "\n[[resource]]\n"
            f'package_id = "{package_id}"\n'
            f'id = "{resource_id}"\n'
            f'path = "{owner_rom.resolve().as_posix()}"\n'
        )
    payload = payload.encode("utf-8")
    atomic_write(state, payload)
    return ModStateGuard(state, original)


def capture(client: TcpClient, step: dict[str, Any], output: pathlib.Path) -> dict[str, Any]:
    ident = step["id"]
    shot = output / f"{ident}.bmp"
    # debug_server receives a raw command line, not a Windows argv.  Backslashes
    # in a relative path would be interpreted as C-style escapes by its parser.
    reply = client.command(f"screenshot {tcp_file_path(shot)}")
    # The server writes synchronously, but a short bounded retry handles host AV/indexing.
    deadline = time.monotonic() + 2.0
    while not shot.is_file() and time.monotonic() < deadline:
        time.sleep(0.02)
    if not shot.is_file():
        raise RuntimeError(f"screenshot command succeeded but did not create {shot}")
    evidence: dict[str, Any] = {
        "id": ident,
        "frame": client.command("frame").get("frame"),
        "screenshot": {"path": shot.name, "sha256": sha256_file(shot), "tcp": reply},
        "wram": [],
    }
    for region in step.get("wram", []):
        address = parse_int(region["addr"], f"{ident}.{region['name']}.addr")
        length = parse_int(region["len"], f"{ident}.{region['name']}.len")
        blob, ram_reply = read_wram(client, address, length)
        evidence["wram"].append({
            "name": region["name"], "addr": f"0x{address:05x}", "len": length,
            "sha256": hashlib.sha256(blob).hexdigest(), "hex": blob.hex(), "tcp": ram_reply,
        })
    return evidence


def wait_ram(client: TcpClient, step: dict[str, Any], pulse: bool = False) -> dict[str, Any]:
    """Advance only in declared frame quanta until a WRAM value is observed."""
    address = parse_int(step["addr"], "wait_ram.addr")
    length = parse_int(step.get("len", 1), "wait_ram.len")
    expected = parse_int(step["equals"], "wait_ram.equals").to_bytes(length, "little")
    timeout_frames = parse_int(step["timeout_frames"], "wait_ram.timeout_frames")
    step_frames = parse_int(step.get("step_frames", 1), "wait_ram.step_frames")
    start_reply = client.command("frame")
    start_frame = start_reply.get("frame")
    if not isinstance(start_frame, int):
        raise RuntimeError(f"frame response lacks integer frame: {start_reply}")
    pulses: list[dict[str, Any]] = []
    while True:
        data, ram_reply = read_wram(client, address, length)
        frame_reply = client.command("frame")
        frame = frame_reply.get("frame")
        if not isinstance(frame, int):
            raise RuntimeError(f"frame response lacks integer frame: {frame_reply}")
        if data == expected:
            return {
                "id": step.get("id"), "addr": f"0x{address:05x}", "len": length,
                "equals": expected.hex(), "observed": data.hex(), "frame": frame,
                "frames_elapsed": frame - start_frame, "read_ram": ram_reply, "pulses": pulses,
            }
        if frame - start_frame >= timeout_frames:
            raise RuntimeError(
                f"wait_ram timed out after {frame - start_frame} frames at 0x{address:05x}: "
                f"wanted {expected.hex()}, observed {data.hex()}")
        if pulse:
            fields = [f"p1={normalize_buttons(step.get('p1'), 'p1')}"]
            if "p2" in step:
                fields.append(f"p2={normalize_buttons(step['p2'], 'p2')}")
            down = client.command("set_controller " + " ".join(fields))
            hold = parse_int(step.get("hold_frames", 1), "pulse_input_until_ram.hold_frames")
            hold_reply = client.command(f"step {hold}")
            up = client.command("set_controller p1=none" + (" p2=none" if "p2" in step else ""))
            pulses.append({"down": down, "hold": hold_reply, "up": up})
        advance = client.command(f"step {step_frames if not pulse else parse_int(step.get('release_frames', 1), 'pulse_input_until_ram.release_frames')}")
        if advance.get("timeout"):
            raise RuntimeError(f"server timed out while advancing wait_ram: {advance}")


def run(args: argparse.Namespace) -> int:
    scenario = load_scenario(args.scenario)
    if not args.exe.is_file():
        message = f"SKIP: Falcon executable not found: {args.exe}"
        print(message)
        return 2 if args.require_build else 0
    args.out.mkdir(parents=True, exist_ok=True)
    mod_state = stage_mod_state(args.exe, scenario.get("mod"), getattr(args, "owner_rom", None))
    process: subprocess.Popen[Any] | None = None
    client: TcpClient | None = None
    manifest: dict[str, Any] = {
        "format": "falcon-validation-evidence/v1",
        "scenario": args.scenario.name,
        "exe": str(args.exe), "port": args.port, "steps": [],
    }
    if scenario.get("mod"):
        manifest["mod"] = scenario["mod"]
    try:
        process = subprocess.Popen(launch_argv(args.exe, args.rom), cwd=str(args.exe.parent))
        client = wait_for_server(process, args.port, args.timeout)
        manifest["start"] = client.command("frame")
        for index, step in enumerate(scenario["steps"]):
            op = step["op"]
            record: dict[str, Any] = {"index": index, "op": op}
            if op == "input":
                fields = [f"p1={normalize_buttons(step.get('p1'), 'p1')}"]
                if "p2" in step:
                    fields.append(f"p2={normalize_buttons(step['p2'], 'p2')}")
                record["tcp"] = client.command("set_controller " + " ".join(fields))
            elif op == "step":
                frames = parse_int(step["frames"], "frames")
                record["requested_frames"] = frames
                record["tcp"] = client.command(f"step {frames}")
            elif op == "wait_ram":
                record.update(wait_ram(client, step))
            elif op == "pulse_input_until_ram":
                record.update(wait_ram(client, step, pulse=True))
            else:
                record.update(capture(client, step, args.out))
            manifest["steps"].append(record)
        manifest["end"] = client.command("frame")
        evidence_path = args.out / "evidence.json"
        evidence_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"PASS: wrote {evidence_path}")
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
        if mod_state:
            mod_state.restore()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    parser.add_argument("--rom", type=pathlib.Path, default=DEFAULT_ROM,
                        help="SMW ROM; required once --exe exists")
    parser.add_argument("--owner-rom", type=pathlib.Path,
                        help="owner ROM for a scenario.mod.resource_id; never recorded in evidence")
    parser.add_argument("--scenario", type=pathlib.Path, default=DEFAULT_SCENARIO)
    parser.add_argument("--out", type=pathlib.Path, default=REPO / "_triage" / "falcon_validation")
    parser.add_argument("--port", type=int, default=4377)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--require-build", action="store_true", help="missing --exe is a failure instead of SKIP")
    args = parser.parse_args()
    try:
        return run(args)
    except (OSError, RuntimeError, ScenarioError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
