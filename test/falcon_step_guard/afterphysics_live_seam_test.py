"""Contract for the generated ordinary PlayerState00 collision seam.

The live $00:CD24 routine falls through into L_CD36_M1X1.  A separate
PlayerState00_00CD36 entry also exists for external dispatch, but is not the
ordinary player-physics path.  Keep the Falcon resolve callback precisely on
the former so a native wall result reaches the low-wall state machine.
"""
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "gen" / "bank00_part09_v2.c"
MARKER = "/*FALCON-AFTER-PHYSICS*/"
ANCHOR = "cpu_trace_block(cpu, 0x00CD36)"


def function_body(text, name):
    match = re.search(
        rf"^RecompReturn\s+{re.escape(name)}\s*\(CpuState \*cpu\)\s*\{{",
        text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing generated function {name}")
    depth = 0
    for pos in range(match.start(), len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():pos + 1]
    raise AssertionError(f"unterminated generated function {name}")


def main():
    text = SOURCE.read_text(encoding="utf-8")
    live = function_body(text, "PlayerState00_00CD24_M1X1")
    standalone = function_body(text, "PlayerState00_00CD36_M1X1")

    assert text.count(MARKER) == 1, "AfterPhysics marker must be unique"
    assert live.count(MARKER) == 1, "live CD24 fall-through owns callback"
    assert standalone.count(MARKER) == 0, "external CD36 entry must not double-resolve"
    assert live.index(ANCHOR) < live.index(MARKER), "callback follows CD36 trace anchor"
    assert "SmwFalconAfterPhysics" in live
    assert "SmwFalconAfterPhysics" not in standalone
    print("afterphysics_live_seam_test: PASS")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
