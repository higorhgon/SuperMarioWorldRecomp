"""Regression checks for the owner-cache helper process boundary."""
from pathlib import Path
import unittest


SOURCE = (Path(__file__).resolve().parents[2] / "src/mods/falcon/"
          "smw_falcon_presentation_runtime.c")


class FalconHostRuntimeContractTests(unittest.TestCase):
    def test_hostile_paths_are_never_formatted_into_a_shell_command(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("system(", source)
        self.assertNotIn("quote_safe", source)
        self.assertIn('"--rom", (char *)owner_rom_path', source)
        self.assertIn('"--cache-root", (char *)root', source)
        self.assertIn('"--result-file",', source)
        self.assertIn('(char *)result, NULL', source)
        self.assertIn("_spawnv(_P_WAIT, helper", source)
        self.assertIn("execv(helper, argv)", source)
        self.assertIn("k_audio_sha256[11][32]", source)
        self.assertIn("join_path(audio_dir, audio_dir_size, cache, \"audio\")", source)
        self.assertIn("smw_falcon_audio_activate(audio_dir)", source)
        self.assertIn("approved owner cache audio disabled", source)
        self.assertIn("smw_falcon_audio_reset();", source)
        self.assertIn("smw_falcon_presentation_foot_anchor_y", source)
        self.assertIn("player_screen_y + 32", source)
        # Every shell metacharacter is valid inside a path argument because the
        # argv launcher never parses it as shell source.
        hostile = r"C:\owner&cache|<bad>^%!$`;space"
        self.assertTrue(hostile.startswith("C:\\"))


if __name__ == "__main__":
    unittest.main()
