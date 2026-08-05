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

    def _run(
        self, repo: Path, *argv: str, protocol: str = "full-control-v2",
    ) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with patch.object(
            play_setup, "_fetch_protocol", return_value=protocol,
        ), redirect_stdout(out), redirect_stderr(err):
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
                "control_protocol": "full-control-v2",
            })
            config_mode = stat.S_IMODE(
                (workspace / ".playconfig.json").stat().st_mode
            )
            self.assertEqual(config_mode, 0o600)
            agents = (workspace / "AGENTS.md").read_text(encoding="utf-8")
            self.assertIn("scratchpad", agents)
            self.assertIn("codex-gpt-5.6-sol", agents)
            self.assertFalse(
                (workspace / "CLAUDE.md").exists(),
                "CLAUDE.md is only for the claude-code harness",
            )
            self.assertIn("full-control-v2", agents)
            self.assertIn("just turn --end --await", agents)
            self.assertIn("cd .play/", out)

    def test_claude_code_harness_gets_claude_md_others_do_not(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, _out, err = self._run(
                repo, GAME_ID,
                "--player", "claude-code:claude-fable-5",
                "--player", "pi:gpt-5.5",
            )
            self.assertEqual(code, 0, err)
            claude_ws = (
                repo / ".play" / f"{GAME_ID}_claude-code_claude-fable-5"
            )
            pi_ws = repo / ".play" / f"{GAME_ID}_pi_gpt-5.5"
            claude = (claude_ws / "CLAUDE.md").read_text(encoding="utf-8")
            self.assertIn("scratchpad", claude)
            self.assertIn("full-control-v2", claude)
            self.assertFalse((pi_ws / "CLAUDE.md").exists())
            self.assertIn(
                "scratchpad",
                (pi_ws / "AGENTS.md").read_text(encoding="utf-8"),
            )

    def test_v1_games_get_the_strategic_loop_note(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, _out, err = self._run(
                repo, GAME_ID,
                "--player", "claude-code:claude-fable-5",
                protocol="strategic-v1",
            )
            self.assertEqual(code, 0, err)
            workspace = (
                repo / ".play" / f"{GAME_ID}_claude-code_claude-fable-5"
            )
            agents = (workspace / "AGENTS.md").read_text(encoding="utf-8")
            claude = (workspace / "CLAUDE.md").read_text(encoding="utf-8")
            for document in (agents, claude):
                self.assertIn("strategic-v1", document)
                self.assertIn("just next --after_turn", document)
                self.assertIn("set_traits", document)
                self.assertNotIn("turn --end --await", document)
            config = json.loads(
                (workspace / ".playconfig.json").read_text(encoding="utf-8")
            )
            self.assertEqual(config["control_protocol"], "strategic-v1")

    def test_unreachable_supervisor_names_the_stack_remedy(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                code = play_setup.main([
                    GAME_ID, "--player", "codex:gpt-5.6-sol",
                    "--repo-root", str(repo),
                ])
            self.assertEqual(code, 2)
            self.assertIn("just start", err.getvalue())

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

    def test_generated_notes_number_join_first_and_start_second(self):
        """A live agent ran `just start` before `just join` and stalled.

        These notes and the workspace's own `just` menu are read before the
        first command, so they must number the same order rather than list an
        unordered set of fast paths.
        """
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, out, err = self._run(
                repo, GAME_ID,
                "--player", "claude-code:claude-fable-5",
                "--player", "pi:gpt-5.5",
            )
            self.assertEqual(code, 0, err)
            claude_ws = (
                repo / ".play" / f"{GAME_ID}_claude-code_claude-fable-5"
            )
            notes = {
                "AGENTS.md": (
                    claude_ws / "AGENTS.md"
                ).read_text(encoding="utf-8"),
                "CLAUDE.md": (
                    claude_ws / "CLAUDE.md"
                ).read_text(encoding="utf-8"),
                "AGENTS.md (pi)": (
                    repo / ".play" / f"{GAME_ID}_pi_gpt-5.5" / "AGENTS.md"
                ).read_text(encoding="utf-8"),
            }
            for name, text in notes.items():
                self.assertRegex(
                    text, r"1\.\s+just join", f"{name} does not number join 1"
                )
                self.assertRegex(
                    text, r"2\.\s+just start",
                    f"{name} does not number start 2",
                )
                self.assertLess(
                    text.index("just join"), text.index("just start"), name,
                )
                self.assertIn("repository stack", text, name)
        # The closing hint the operator hands to each model names join too.
        self.assertIn("&& just join", out)

    def test_v1_notes_number_join_first_as_well(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = self._repo(directory)
            code, _out, err = self._run(
                repo, GAME_ID, "--player", "codex:gpt-5.6-sol",
                protocol="strategic-v1",
            )
            self.assertEqual(code, 0, err)
            agents = (
                repo / ".play" / f"{GAME_ID}_codex_gpt-5.6-sol" / "AGENTS.md"
            ).read_text(encoding="utf-8")
            self.assertRegex(agents, r"1\.\s+just join")
            self.assertRegex(agents, r"2\.\s+just next")
            self.assertIn("strategic-v1", agents)


if __name__ == "__main__":
    unittest.main()
