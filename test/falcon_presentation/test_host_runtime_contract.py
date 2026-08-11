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
        self.assertIn("smw_falcon_audio_activate(s_validated_audio_dir)", source)
        self.assertIn("approved owner cache audio disabled", source)
        self.assertIn("smw_falcon_audio_reset();", source)
        self.assertIn("smw_falcon_presentation_foot_anchor_y", source)
        self.assertIn("player_screen_y + 32", source)
        # Every shell metacharacter is valid inside a path argument because the
        # argv launcher never parses it as shell source.
        hostile = r"C:\owner&cache|<bad>^%!$`;space"
        self.assertTrue(hostile.startswith("C:\\"))

    def test_course_clear_is_presentation_only_and_excludes_demo_keyhole(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("static int course_clear_active(void)", source)
        self.assertIn("snes_foreign_ownership() == FOREIGN_OWNERSHIP_SCRIPTED", source)
        self.assertIn("misc_game_mode == 0x14 && player_current_state == 0", source)
        self.assertIn("timer_end_level != 0 && timer_end_level_via_keyhole == 0", source)
        self.assertIn("flag_show_victory_pose_during_level_end != 0", source)
        self.assertIn("return controllable() || death_active() || course_clear_active();", source)
        self.assertIn("if (!presentation_active()) { s_suppression_active = 0; return; }", source)
        self.assertIn("if (!presentation_active() || !pixels", source)
        # Carry OAM mutation remains live-control-only; Course Clear merely
        # hides PlayerGFXRt and draws Falcon over the native score script.
        self.assertIn("if (!ppu || !controllable()) return;", source)


if __name__ == "__main__":
    unittest.main()
