# SPDX-License-Identifier: GPL-3.0-or-later
"""Play-disc linking with and without movies, on a synthetic disc."""
import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build_game
from build_game import MOVIE_STUB, cut_owned_movies, find_disc, prepare_play_disc
from extract_iso import sha256


def sha256_of(data):
    return hashlib.sha256(data).hexdigest()


def fake_extract(iso, output, tool):
    """extract-xiso stand-in: the ISO's bytes become default.xbe."""
    (output / "disc").mkdir(parents=True)
    (output / "disc" / "default.xbe").write_bytes(iso.read_bytes())
    (output / "receipt.json").write_text(f'{{"iso_sha256": "{sha256(iso)}"}}', encoding="utf-8")


class PlayDiscTests(unittest.TestCase):
    def test_movies_are_cut_and_restored(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(build_game, "ROOT", Path(folder)):
            root = Path(folder)
            disc, play = root / "user disc", root / "private" / "play-disc"
            (disc / ".recomp-storage").mkdir(parents=True)
            (disc / ".recomp-storage" / "save").write_bytes(b"save")
            (disc / "default.xbe").write_bytes(b"xbe")
            movie = bytes(range(256)) * 64
            (disc / "mv_st.sfd").write_bytes(movie)
            (disc / "short.sfd").write_bytes(b"tiny")

            prepare_play_disc(disc, movies=False)
            self.assertEqual((play / "mv_st.sfd").read_bytes(), movie[:MOVIE_STUB])
            self.assertEqual((disc / "mv_st.sfd").read_bytes(), movie, "a user's folder is never cut")
            self.assertEqual((play / "short.sfd").read_bytes(), b"tiny", "a short file is never padded")
            self.assertTrue(os.path.samefile(play / "default.xbe", disc / "default.xbe"))
            self.assertFalse((play / ".recomp-storage").exists())
            cut_owned_movies(disc)
            self.assertEqual((disc / "mv_st.sfd").read_bytes(), movie, "a user's folder is never cut")

            prepare_play_disc(disc, movies=True)
            self.assertTrue(os.path.samefile(play / "mv_st.sfd", disc / "mv_st.sfd"))

            # An extraction the build owns stays whole until the build succeeds.
            owned = root / "private" / "imported-disc" / "disc"
            owned.parent.mkdir(parents=True)
            disc.rename(owned)
            prepare_play_disc(owned, movies=False)
            self.assertEqual((owned / "mv_st.sfd").read_bytes(), movie)
            self.assertEqual((play / "mv_st.sfd").read_bytes(), movie[:MOVIE_STUB])
            cut_owned_movies(owned)
            self.assertEqual((owned / "mv_st.sfd").stat().st_size, MOVIE_STUB)
            self.assertEqual((owned / "short.sfd").read_bytes(), b"tiny")
            self.assertTrue(build_game.movies_cut(owned))
            self.assertEqual((play / "mv_st.sfd").read_bytes(), movie[:MOVIE_STUB])

    def test_cut_extraction_is_rebuilt_or_restored(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(build_game, "ROOT", Path(folder)):
            imported = Path(folder) / "private" / "imported-disc"
            (imported / "disc").mkdir(parents=True)
            (imported / "disc" / "mv_st.sfd").write_bytes(bytes(MOVIE_STUB))
            iso = Path(folder) / "doa3.iso"
            iso.write_bytes(b"iso")
            (imported / "receipt.json").write_text(f'{{"iso_sha256": "{sha256(iso)}"}}', encoding="utf-8")

            # Without an ISO a rebuild keeps the movies out instead of failing.
            self.assertFalse(build_game.ask_movies(None))
            self.assertEqual(find_disc(None, movies=False), imported / "disc")
            with self.assertRaisesRegex(ValueError, "restore them"):
                find_disc(None, movies=True)
            # The same ISO with movies re-extracts.
            with patch.object(build_game, "extract", side_effect=fake_extract), \
                    patch.object(build_game, "VERSIONS", {sha256_of(b"iso"): {"suffix": ""}}):
                self.assertEqual(find_disc(iso, movies=True), imported / "disc")
            self.assertEqual((imported / "disc" / "default.xbe").read_bytes(), b"iso")
            self.assertFalse(build_game.movies_cut(imported / "disc"))

    def test_each_version_keeps_its_own_extraction(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(build_game, "ROOT", Path(folder)), \
                patch.object(build_game, "extract", side_effect=fake_extract), \
                patch.object(build_game, "VERSIONS", {sha256_of(b"jp"): {"suffix": "-jp"},
                                                      sha256_of(b"usa"): {"suffix": ""}}):
            private = Path(folder) / "private"
            usa = private / "imported-disc"
            (usa / "disc").mkdir(parents=True)
            (usa / "disc" / "default.xbe").write_bytes(b"usa")
            (usa / "receipt.json").write_text('{"iso_sha256": "other"}', encoding="utf-8")
            iso = Path(folder) / "jp.iso"
            iso.write_bytes(b"jp")

            self.assertEqual(find_disc(iso), private / "imported-disc-jp" / "disc")
            self.assertEqual((usa / "disc" / "default.xbe").read_bytes(), b"usa", "USA extraction untouched")
            self.assertFalse((private / "imported-disc.new").exists())
            # The same ISO again reuses its extraction.
            with patch.object(build_game, "extract") as extract:
                self.assertEqual(find_disc(iso), private / "imported-disc-jp" / "disc")
            extract.assert_not_called()
            with self.assertRaisesRegex(ValueError, "Japan 3.1 and USA 3.0 only"):
                build_game.disc_version(Path(folder))

    def test_closed_stdin_installs_movies(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(build_game, "ROOT", Path(folder)), \
                patch("sys.stdin.isatty", return_value=True), patch("builtins.input", side_effect=EOFError):
            self.assertTrue(build_game.ask_movies(None))


if __name__ == "__main__":
    unittest.main()
