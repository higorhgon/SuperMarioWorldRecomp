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
        self.assertEqual(len(scenario["steps"]), 7)

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
                "timeout": 0.1, "require_build": False,
            })()
            self.assertEqual(falcon.run(args), 0)


if __name__ == "__main__":
    unittest.main()
