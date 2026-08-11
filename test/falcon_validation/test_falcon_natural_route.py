"""Contracts for the free-running natural Falcon route scout."""
from __future__ import annotations

import importlib.util
import pathlib
import sys
import tempfile
import unittest


REPO = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("falcon_natural_route", REPO / "tools" / "falcon_natural_route.py")
assert SPEC and SPEC.loader
route = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = route
SPEC.loader.exec_module(route)


class NaturalRouteTests(unittest.TestCase):
    def test_load_signature_matches_saved_player_position(self) -> None:
        player = bytearray(42)
        player[0] = 0x00
        player[0x94 - 0x71:0x96 - 0x71] = bytes((0xF2, 0x06))
        player[0x96 - 0x71:0x98 - 0x71] = bytes((0x60, 0x01))
        sample = {"game_mode": "14", "player": player.hex()}
        self.assertTrue(route.matches_load_signature(sample, {
            "game_mode": "14", "player_state": "00", "x": "06f2", "y": "0160",
        }))
        self.assertFalse(route.matches_load_signature(sample, {
            "game_mode": "14", "player_state": "00", "x": "0742", "y": "0160",
        }))

    def test_rejects_an_invalid_checkpoint_slot(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / "bad.json"
            path.write_text("""{
              "format": "falcon-native-route/v1", "slot": 0,
              "inputs": [{"at": 0, "p1": "none"}],
              "savestates": [{"at": 1, "id": "bad", "slot": 12}]
            }""", encoding="utf-8")
            with self.assertRaisesRegex(route.RouteError, "slots 0 through 11"):
                route.load_route(path)


if __name__ == "__main__":
    unittest.main()
