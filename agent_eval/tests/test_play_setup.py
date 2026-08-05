"""Tests for the `just play` per-player workspace materializer."""

from __future__ import annotations

import io
import json
import stat
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

from agent_eval import play_setup

GAME_ID = "game_xMCbmQ67I89z0UjFTM8zyO9H"


class PlaySetupTests(unittest.TestCase):
    def _repo(self, directory: str) -> Path:
        repo = Path(directory)
        play = repo / "play"
        (play / "docs").mkdir(parents=True)
        (play / ".sessions").mkdir(mode=0o700)
        (play / "__pycache__").mkdir()
        (play / "client.py").write_text("# client\n", encoding="utf-8")
        (play / "justfile").write_text("# recipes\n", encoding="utf-8")
        (play / "play").write_text("#!/usr/bin/env python3\n", encoding="utf-8")
        (play / "AGENTS.md").write_text("# Boundary\n", encoding="utf-8")
        (play / "docs" / "play.md").write_text("card\n", encoding="utf-8")
        (play / "stale.jsonl").write_text("{}\n", encoding="utf-8")
        invites = play / ".invites"
        invites.mkdir(mode=0o700)
        (invites / f"{GAME_ID}.json").write_text(
            json.dumps({
                "schema_version": 1,
                "game_id": GAME_ID,
                "service_url": "http://127.0.0.1:1",
                "join_token": "token",
            }),
            encoding="utf-8",
        )
        return repo

    def _run(self, repo: Path, *argv: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = play_setup.main([
                *argv, "--repo-root", str(repo),
            ])
        return code, out.getvalue(), err.getvalue()

    def test_materializes_a_configured_scratchpad_workspace(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, out, err = self._run(
                repo, GAME_ID, "--player", "codex:gpt-5.6-sol",
            )
            self.assertEqual(code, 0, err)
            workspace = repo / ".play" / f"{GAME_ID}_codex_gpt-5.6-sol"
            self.assertTrue((workspace / "client.py").is_file())
            self.assertTrue((workspace / "play").is_file())
            self.assertFalse((workspace / "stale.jsonl").exists())
            self.assertFalse((workspace / "__pycache__").exists())
            for name in (".sessions", ".invites"):
                mode = stat.S_IMODE((workspace / name).stat().st_mode)
                self.assertEqual(mode, 0o700, name)
            invite = workspace / ".invites" / f"{GAME_ID}.json"
            self.assertTrue(invite.is_file())
            config = json.loads(
                (workspace / ".playconfig.json").read_text(encoding="utf-8")
            )
            self.assertEqual(config, {
                "schema_version": 1,
                "game_id": GAME_ID,
                "name": "codex-gpt-5.6-sol",
                "place": None,
            })
            config_mode = stat.S_IMODE(
                (workspace / ".playconfig.json").stat().st_mode
            )
            self.assertEqual(config_mode, 0o600)
            agents = (workspace / "AGENTS.md").read_text(encoding="utf-8")
            self.assertIn("scratchpad", agents)
            self.assertIn("codex-gpt-5.6-sol", agents)
            claude = (workspace / "CLAUDE.md").read_text(encoding="utf-8")
            self.assertIn("just join", claude)
            self.assertIn("scratchpad", claude)
            self.assertIn("cd .play/", out)

    def test_multiple_players_get_sequential_places(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, _out, err = self._run(
                repo, GAME_ID,
                "--player", "codex:gpt-5.6-sol",
                "--player", "claude-code:claude-fable-5",
            )
            self.assertEqual(code, 0, err)
            first = json.loads((
                repo / ".play" / f"{GAME_ID}_codex_gpt-5.6-sol"
                / ".playconfig.json"
            ).read_text(encoding="utf-8"))
            second = json.loads((
                repo / ".play" / f"{GAME_ID}_claude-code_claude-fable-5"
                / ".playconfig.json"
            ).read_text(encoding="utf-8"))
            self.assertEqual(first["place"], 1)
            self.assertEqual(second["place"], 2)

    def test_existing_workspace_refuses_without_force(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, _out, err = self._run(
                repo, GAME_ID, "--player", "codex:gpt-5.6-sol",
            )
            self.assertEqual(code, 0, err)
            marker = (
                repo / ".play" / f"{GAME_ID}_codex_gpt-5.6-sol" / "notes.md"
            )
            marker.write_text("my plan\n", encoding="utf-8")
            code, _out, err = self._run(
                repo, GAME_ID, "--player", "codex:gpt-5.6-sol",
            )
            self.assertEqual(code, 2)
            self.assertIn("--force", err)
            self.assertTrue(marker.is_file())
            code, _out, err = self._run(
                repo, GAME_ID, "--player", "codex:gpt-5.6-sol", "--force",
            )
            self.assertEqual(code, 0, err)
            self.assertFalse(marker.exists())

    def test_bad_game_id_and_bad_player_fail_with_remedies(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, _out, err = self._run(
                repo, "not-a-game", "--player", "codex:gpt-5.6-sol",
            )
            self.assertEqual(code, 2)
            self.assertIn("game_", err)
            code, _out, err = self._run(repo, GAME_ID, "--player", "codex")
            self.assertEqual(code, 2)
            self.assertIn("HARNESS:MODEL", err)

    def test_missing_invite_names_the_owner_remedy(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            (repo / "play" / ".invites" / f"{GAME_ID}.json").unlink()
            code, _out, err = self._run(
                repo, GAME_ID, "--player", "codex:gpt-5.6-sol",
            )
            self.assertEqual(code, 2)
            self.assertIn(f"just invite {GAME_ID}", err)

    def test_interactive_flow_builds_players_from_menus(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            answers = iter([
                GAME_ID,  # game id prompt
                "1",      # harness: codex
                "1",      # model: gpt-5.6-sol
                "n",      # add another? no
            ])
            with patch("builtins.input", side_effect=lambda *a: next(answers)):
                code, _out, err = self._run(repo)
            self.assertEqual(code, 0, err)
            workspace = repo / ".play" / f"{GAME_ID}_codex_gpt-5.6-sol"
            self.assertTrue((workspace / ".playconfig.json").is_file())


if __name__ == "__main__":
    unittest.main()
