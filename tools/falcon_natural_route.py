#!/usr/bin/env python3
"""Free-running, no-write scout for natural Falcon validation routes.

This deliberately has no pause, step, write_ram, or synthetic-sprite path.
It loads an existing native save at a main-thread frame boundary, applies only
controller input, and records the natural sprite table plus final PPU shots.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import socket
import subprocess
import time
from typing import Any


REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_EXE = REPO / "build-falcon" / "SuperMarioWorldSNESRecomp.exe"
DEFAULT_ROM = REPO / "build-falcon" / "smw.sfc"
DEFAULT_ROUTE = REPO / "test" / "falcon_validation" / "falcon_natural_route_slot0.json"


class RouteError(RuntimeError):
    pass


class Tcp:
    def __init__(self, port: int) -> None:
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.file = self.sock.makefile("rb")

    def command(self, line: str) -> dict[str, Any]:
        self.sock.sendall((line + "\n").encode("ascii"))
        reply = json.loads(self.file.readline())
        if not isinstance(reply, dict) or reply.get("ok") is False:
            raise RouteError(f"rejected {line!r}: {reply!r}")
        return reply

    def read(self, address: int, length: int) -> str:
        reply = self.command(f"read_ram {address:x} {length}")
        value = reply.get("hex")
        if not isinstance(value, str):
            raise RouteError(f"read_ram lacks hex: {reply!r}")
        return value.replace(" ", "")

    def close(self) -> None:
        self.file.close()
        self.sock.close()


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def port_is_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.settimeout(0.2)
        return probe.connect_ex(("127.0.0.1", port)) != 0


def require_port_free(port: int) -> None:
    if not port_is_free(port):
        raise RouteError(f"TCP port {port} is already occupied")


def load_route(path: pathlib.Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("format") != "falcon-native-route/v1":
        raise RouteError("unexpected route format")
    if not isinstance(data.get("slot"), int) or not 0 <= data["slot"] <= 11:
        raise RouteError("slot must be 0 through 11")
    if not isinstance(data.get("inputs"), list) or not data["inputs"]:
        raise RouteError("route needs inputs")
    previous = -1
    for event in data["inputs"]:
        if not isinstance(event, dict) or not isinstance(event.get("at"), int) or event["at"] < previous:
            raise RouteError("inputs must be ordered frame offsets")
        if not isinstance(event.get("p1"), str):
            raise RouteError("input p1 must be a controller string")
        previous = event["at"]
    captures = data.get("captures", [])
    if not isinstance(captures, list):
        raise RouteError("captures must be a list")
    previous = -1
    for capture in captures:
        if (not isinstance(capture, dict) or not isinstance(capture.get("at"), int)
                or capture["at"] < previous or not isinstance(capture.get("id"), str)):
            raise RouteError("captures need ordered integer at values and string ids")
        previous = capture["at"]
    saves = data.get("savestates", [])
    if not isinstance(saves, list):
        raise RouteError("savestates must be a list")
    previous = -1
    for save in saves:
        if (not isinstance(save, dict) or not isinstance(save.get("at"), int)
                or save["at"] < previous or not isinstance(save.get("slot"), int)
                or not 0 <= save["slot"] <= 11 or not isinstance(save.get("id"), str)):
            raise RouteError("savestates need ordered at values, ids, and slots 0 through 11")
        previous = save["at"]
    signature = data.get("load_signature")
    if signature is not None:
        if not isinstance(signature, dict):
            raise RouteError("load_signature must be an object")
        for key in ("game_mode", "player_state", "x", "y"):
            if not isinstance(signature.get(key), str):
                raise RouteError(f"load_signature needs string {key}")
    target = data.get("target_checkpoint")
    if target is not None:
        if (not isinstance(target, dict) or not isinstance(target.get("id"), str)
                or not isinstance(target.get("slot"), int) or not 0 <= target["slot"] <= 11
                or not isinstance(target.get("statuses"), list)
                or not target["statuses"] or any(not isinstance(state, int) or not 0 <= state <= 0xFF
                                                   for state in target["statuses"])):
            raise RouteError("target_checkpoint needs id, slot 0 through 11, and status bytes")
    return data


def write_no_gamepad_config(path: pathlib.Path) -> None:
    # A temporary external config prevents a physical controller from merging
    # with the declared test timeline. It never alters the integration build.
    path.write_text("[General]\nAutosave = 0\nSkipLauncher = 1\n\n"
                    "[GamepadMap]\nEnableGamepad1 = false\nEnableGamepad2 = false\n",
                    encoding="utf-8")


def wait_for_tcp(process: subprocess.Popen[Any], port: int) -> Tcp:
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RouteError(f"runner exited early ({process.returncode})")
        try:
            client = Tcp(port)
            ready = time.monotonic() + 20
            while time.monotonic() < ready:
                reply = client.command("read_ram 100 1")
                if isinstance(reply.get("hex"), str) and len(reply["hex"].replace(" ", "")) == 2:
                    return client
                time.sleep(0.05)
            client.close()
            raise RouteError("WRAM did not become ready")
        except OSError:
            time.sleep(0.05)
    raise RouteError("TCP listener did not become ready")


def snapshot(client: Tcp) -> dict[str, Any]:
    # 12 native normal-sprite slots, captured without changing their lifecycle.
    return {
        "frame": client.command("frame").get("frame"),
        "game_mode": client.read(0x0100, 1),
        "player": client.read(0x0071, 42),
        "player_subposition": client.read(0x13DA, 4),
        "carry_flags": client.read(0x1470, 0x20),
        "sprite_ids": client.read(0x009E, 12),
        "sprite_status": client.read(0x14C8, 12),
        "sprite_y_lo": client.read(0x00D8, 12), "sprite_y_hi": client.read(0x14D4, 12),
        "sprite_x_lo": client.read(0x00E4, 12), "sprite_x_hi": client.read(0x14E0, 12),
        "sprite_y_speed": client.read(0x00AA, 12), "sprite_x_speed": client.read(0x00B6, 12),
        "sprite_subposition": client.read(0x14EC, 12) + client.read(0x14F8, 12),
        "sprite_oam": client.read(0x15EA, 12),
    }


def find_interesting(sample: dict[str, Any]) -> list[dict[str, int]]:
    ids = bytes.fromhex(sample["sprite_ids"])
    states = bytes.fromhex(sample["sprite_status"])
    found = []
    for slot, (sprite_id, state) in enumerate(zip(ids, states)):
        if state in (0x08, 0x09, 0x0A, 0x0B):
            found.append({"slot": slot, "id": sprite_id, "status": state})
    return found


def matches_load_signature(sample: dict[str, Any], signature: dict[str, str]) -> bool:
    player = bytes.fromhex(sample["player"])
    x = player[0x94 - 0x71] | player[0x95 - 0x71] << 8
    y = player[0x96 - 0x71] | player[0x97 - 0x71] << 8
    return (sample["game_mode"].lower() == signature["game_mode"].lower()
            and f"{player[0]:02x}" == signature["player_state"].lower()
            and f"{x:04x}" == signature["x"].lower()
            and f"{y:04x}" == signature["y"].lower())


def capture(client: Tcp, output: pathlib.Path, ident: str, evidence: dict[str, Any]) -> None:
    image = output / f"{ident}.bmp"
    reply = client.command("screenshot " + str(image).replace("\\", "/"))
    deadline = time.monotonic() + 2
    while not image.is_file() and time.monotonic() < deadline:
        time.sleep(0.02)
    if not image.is_file():
        raise RouteError(f"screenshot did not produce {image}")
    state = snapshot(client)
    # The PPU render ring is read-only and reports what the scanline renderer
    # consumed, not merely WRAM's requested OAM selector.
    oam = client.command("oam_render_get 4 128")
    evidence["captures"].append({"id": ident, "screenshot": image.name,
        "screenshot_sha256": sha256(image), "screenshot_tcp": reply,
        "state": state, "oam_render": oam})


def run(args: argparse.Namespace) -> int:
    route = load_route(args.route)
    if not args.exe.is_file() or not args.rom.is_file():
        raise RouteError("Falcon executable or ROM is missing")
    require_port_free(args.port)
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    for save in route.get("savestates", []):
        # A validation checkpoint must never overwrite a pre-existing native
        # save.  Its only allowed write is a newly requested main-thread slot.
        if (args.exe.parent / "saves" / f"save{save['slot']}.sav").exists():
            raise RouteError(f"refusing to overwrite existing save{save['slot']}.sav")
    target = route.get("target_checkpoint")
    if target and (args.exe.parent / "saves" / f"save{target['slot']}.sav").exists():
        raise RouteError(f"refusing to overwrite existing save{target['slot']}.sav")
    config = args.out / "no_gamepad.ini"
    write_no_gamepad_config(config)
    cache_root = pathlib.Path(os.environ["LOCALAPPDATA"]) / "SuperMarioWorldRecomp" / "smash64"
    cache = next(cache_root.glob("falcon-final-r1-*"), None)
    if cache is None:
        raise RouteError("approved Falcon presentation cache is absent")
    environment = os.environ.copy()
    environment["SNESRECOMP_FALCON_CACHE"] = str(cache)
    process = subprocess.Popen([str(args.exe), "--config", str(config), str(args.rom)],
                               cwd=str(args.exe.parent), env=environment)
    client: Tcp | None = None
    evidence: dict[str, Any] = {"format": "falcon-native-route-evidence/v1",
        "route": args.route.name, "exe": str(args.exe), "exe_sha256": sha256(args.exe),
        "route_sha256": sha256(args.route), "cache": str(cache),
        "cache_manifest_sha256": sha256(cache / "manifest.json"),
        "cache_runtime_sha256": sha256(cache / "falcon_runtime.bin"),
        "captures": [], "samples": []}
    try:
        client = wait_for_tcp(process, args.port)
        boot_start = client.command("frame").get("frame")
        if not isinstance(boot_start, int):
            raise RouteError("frame counter is absent")
        while True:
            boot_frame = client.command("frame").get("frame")
            if not isinstance(boot_frame, int):
                raise RouteError("frame counter is absent")
            if boot_frame - boot_start >= int(route.get("boot_wait_frames", 180)):
                break
            time.sleep(0.008)
        # Loading is asynchronous: it is consumed only by main's normal frame
        # boundary; the runner is never halted.
        client.command("set_controller p1=none")
        client.command(f"loadstate {route['slot']}")
        signature = route.get("load_signature")
        if signature:
            deadline = time.monotonic() + 5
            stable_frame = -1
            while time.monotonic() < deadline:
                loaded = snapshot(client)
                if matches_load_signature(loaded, signature):
                    loaded_frame = loaded.get("frame")
                    if isinstance(loaded_frame, int):
                        stable_frame = loaded_frame
                        evidence["load_signature"] = {"matched_frame": stable_frame,
                            "state": loaded}
                        break
                time.sleep(0.008)
            if stable_frame < 0:
                raise RouteError("loadstate did not reach the declared signature")
            # Give the asynchronous handoff two ordinary running frames before
            # the timeline begins; neither the main loop nor the CPU is paused.
            while True:
                frame_after_load = client.command("frame").get("frame")
                if isinstance(frame_after_load, int) and frame_after_load - stable_frame >= 2:
                    start = frame_after_load
                    break
                time.sleep(0.008)
        else:
            start = client.command("frame").get("frame")
        if not isinstance(start, int):
            raise RouteError("frame counter is absent")
        event_index = 0
        capture_index = 0
        save_index = 0
        last_sample_frame = -1
        captured_loaded = False
        found: set[tuple[int, int, int]] = set()
        checkpointed_target = False
        end_offset = int(route.get("route_frames", 420))
        while True:
            frame_reply = client.command("frame").get("frame")
            if not isinstance(frame_reply, int):
                raise RouteError("frame counter is absent")
            elapsed = frame_reply - start
            while event_index < len(route["inputs"]) and elapsed >= route["inputs"][event_index]["at"]:
                client.command("set_controller p1=" + route["inputs"][event_index]["p1"])
                event_index += 1
            while capture_index < len(route.get("captures", [])) and elapsed >= route["captures"][capture_index]["at"]:
                capture(client, args.out, route["captures"][capture_index]["id"], evidence)
                capture_index += 1
            while save_index < len(route.get("savestates", [])) and elapsed >= route["savestates"][save_index]["at"]:
                save = route["savestates"][save_index]
                reply = client.command(f"savestate {save['slot']}")
                evidence.setdefault("savestates", []).append({
                    "id": save["id"], "slot": save["slot"], "requested_elapsed": elapsed,
                    "requested_frame": frame_reply, "reply": reply,
                })
                save_index += 1
            if frame_reply != last_sample_frame:
                sample = snapshot(client); sample["elapsed"] = elapsed
                interesting = find_interesting(sample); sample["interesting"] = interesting
                evidence["samples"].append(sample); last_sample_frame = frame_reply
                if not captured_loaded and elapsed >= int(route.get("settle_frames", 10)) and sample["game_mode"] == "14":
                    capture(client, args.out, "slot_loaded", evidence); captured_loaded = True
                for candidate in interesting:
                    key = (candidate["slot"], candidate["id"], candidate["status"])
                    if key not in found:
                        found.add(key)
                        capture(client, args.out, f"sprite_s{candidate['slot']}_id{candidate['id']:02x}_st{candidate['status']:02x}", evidence)
                    if (target and not checkpointed_target
                            and candidate["status"] in target["statuses"]):
                        # Stop input before the next main frame, then ask that
                        # same unpaused main thread to preserve the real target.
                        client.command("set_controller p1=none")
                        reply = client.command(f"savestate {target['slot']}")
                        evidence.setdefault("savestates", []).append({
                            "id": target["id"], "slot": target["slot"],
                            "requested_elapsed": elapsed, "requested_frame": frame_reply,
                            "reply": reply, "target": candidate, "precontact": sample,
                        })
                        evidence["stop_reason"] = f"target_checkpoint_{target['id']}"
                        checkpointed_target = True
                if checkpointed_target:
                    break
                # A route whose player state became death is not a valid natural
                # target route.  Stop at the first observed state instead of
                # continuing to the overworld and obscuring the failure point.
                if sample["player"][:2].lower() == "09":
                    evidence["stop_reason"] = "player_state_09"
                    break
            if elapsed >= end_offset:
                break
            time.sleep(0.008)
        client.command("set_controller p1=none")
        capture(client, args.out, "route_end", evidence)
        evidence["end"] = snapshot(client)
        for saved in evidence.get("savestates", []):
            path = args.exe.parent / "saves" / f"save{saved['slot']}.sav"
            deadline = time.monotonic() + 2
            while not path.is_file() and time.monotonic() < deadline:
                time.sleep(0.02)
            if not path.is_file():
                raise RouteError(f"save{saved['slot']}.sav was not materialized")
            saved["path"] = str(path)
            saved["sha256"] = sha256(path)
        (args.out / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        print(f"PASS: wrote {args.out / 'evidence.json'}")
        return 0
    finally:
        if client:
            client.close()
        if process.poll() is None:
            process.terminate(); process.wait(timeout=8)
        if not port_is_free(args.port):
            raise RouteError("port was not released")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=pathlib.Path, default=DEFAULT_EXE)
    parser.add_argument("--rom", type=pathlib.Path, default=DEFAULT_ROM)
    parser.add_argument("--route", type=pathlib.Path, default=DEFAULT_ROUTE)
    parser.add_argument("--out", type=pathlib.Path, default=REPO / "_triage" / "falcon_natural_route")
    parser.add_argument("--port", type=int, default=4377)
    args = parser.parse_args()
    try:
        return run(args)
    except (OSError, RouteError, json.JSONDecodeError) as exc:
        print(f"FAIL: {exc}", file=os.sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
