"""Focused provenance and cache-safety tests for the offline SSB64 pipeline."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest


REPO = pathlib.Path(__file__).resolve().parents[2]
TOOLS = REPO / "tools" / "owner_ssb64"


def load_module(name: str):
    spec = importlib.util.spec_from_file_location(name, TOOLS / f"{name}.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


build_cache = load_module("build_cache")
owner_audio = load_module("owner_audio")
decode_intermediates = load_module("decode_intermediates")
bake_falcon_runtime = load_module("bake_falcon_runtime")
build_final_cache = load_module("build_final_cache")


class OwnerSsb64Tests(unittest.TestCase):
    def test_normalizes_all_three_n64_byte_orders(self) -> None:
        canonical = bytearray(build_cache.CANONICAL_SIZE)
        canonical[:4] = b"\x80\x37\x12\x40"
        canonical = bytes(canonical)
        v64 = bytearray(canonical)
        v64[0::2], v64[1::2] = canonical[1::2], canonical[0::2]
        n64 = bytearray(canonical)
        n64[0::4], n64[1::4] = canonical[3::4], canonical[2::4]
        n64[2::4], n64[3::4] = canonical[1::4], canonical[0::4]
        self.assertEqual(build_cache.normalize_rom(canonical), canonical)
        self.assertEqual(build_cache.normalize_rom(bytes(v64)), canonical)
        self.assertEqual(build_cache.normalize_rom(bytes(n64)), canonical)
        self.assertEqual(owner_audio.normalize_rom(bytes(v64)), canonical)

    def test_wrong_normalized_sha1_cannot_create_a_cache(self) -> None:
        fake_rom = bytearray(build_cache.CANONICAL_SIZE)
        fake_rom[:4] = b"\x80\x37\x12\x40"
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            rom = root / "not-owner-rom.z64"
            cache = root / "cache"
            rom.write_bytes(fake_rom)
            with self.assertRaisesRegex(ValueError, "US v1.0"):
                build_cache.build_cache(rom, cache)
            self.assertFalse(cache.exists())

    def test_cache_manifest_has_only_normalized_identity_and_artifact_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            reloc = root / "reloc"
            reloc.mkdir()
            artifacts = []
            for file_id, name, size in build_cache.RECIPE_FILES:
                payload = bytes([file_id & 0xFF]) * size
                filename = f"{file_id:04d}_{name}.bin"
                (reloc / filename).write_bytes(payload)
                artifacts.append({
                    "file_id": file_id, "name": name, "size": size,
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "reloc": {"compressed": False, "data_offset": 0,
                              "compressed_bytes": size, "decompressed_bytes": size,
                              "reloc_intern_word": 0, "reloc_extern_word": 0},
                })
            manifest = {"format": "smw-smash64-falcon-reloc-cache",
                        "recipe_version": build_cache.RECIPE_VERSION,
                        "normalized_rom_sha1": build_cache.CANONICAL_SHA1,
                        "artifacts": artifacts}
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            checked = build_cache.verify_cache(root, build_cache.CANONICAL_SHA1)
            self.assertEqual(checked["normalized_rom_sha1"], build_cache.CANONICAL_SHA1)
            self.assertNotIn("rom_path", checked)
            self.assertNotIn("rom", checked)

    def test_source_tree_is_never_a_cache_destination(self) -> None:
        with self.assertRaisesRegex(ValueError, "outside the source tree"):
            build_cache._require_external_cache_root(REPO)
        with self.assertRaisesRegex(ValueError, "outside the source tree"):
            owner_audio._external_output(REPO)

    def test_recipe_matches_the_public_us_reloc_table_contract(self) -> None:
        # Cross-checked read-only against SmashBrosDecomp 054ffc23 and
        # BattleShip 4fc11288: their public parser/table declarations use the
        # same US NALE base, entry width, and file count.
        self.assertEqual(build_cache.RELOC_TABLE_ROM_ADDR, 0x001AC870)
        self.assertEqual(build_cache.RELOC_TABLE_ENTRY_SIZE, 12)
        self.assertEqual(build_cache.RELOC_FILE_COUNT, 2132)
        ids = {row[0] for row in build_cache.RECIPE_FILES}
        self.assertTrue({332, 333, 350, 1512, 1652, 1661}.issubset(ids))

    def test_final_cache_uses_the_smw_product_identity(self) -> None:
        self.assertEqual(build_final_cache.FINAL_FORMAT, "smw-smash64-falcon-owner-cache")
        self.assertIn("SuperMarioWorldRecomp", str(build_final_cache.default_cache_root()))
        self.assertEqual(decode_intermediates.MODEL_ID, 332)


if __name__ == "__main__":
    unittest.main()
