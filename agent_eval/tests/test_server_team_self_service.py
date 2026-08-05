from __future__ import annotations

from pathlib import Path
import unittest


class ServerTeamSelfServiceTests(unittest.TestCase):
    def test_basic_player_can_only_bypass_vote_for_own_pregame_team(self) -> None:
        source = (
            Path(__file__).resolve().parents[2] / "server" / "stdinhand.c"
        ).read_text(encoding="utf-8")

        helper = source.split(
            "static bool team_command_is_self_service", 1
        )[1].split("static bool team_command_self_service_allowed", 1)[0]
        allowed = source.split(
            "static bool team_command_self_service_allowed", 1
        )[1].split("static bool team_command(", 1)[0]
        team_command = source.split(
            "static bool team_command(", 1
        )[1].split("static void show_votes", 1)[0]
        dispatcher = source.split(
            "static bool handle_stdin_input_real", 2
        )[2].split("switch (cmd)", 1)[0]

        self.assertIn("conn_get_access(caller) != ALLOW_BASIC", helper)
        self.assertIn("server_state() != S_S_INITIAL", helper)
        self.assertIn("pplayer = player_by_name_prefix", helper)
        self.assertIn("pplayer == conn_get_player(caller)", helper)

        self.assertIn("server_state() != S_S_INITIAL", allowed)
        self.assertIn("!game.info.is_new_game", allowed)
        self.assertIn("pplayer->is_ready", allowed)
        self.assertIn("pplayer->team == nullptr", allowed)
        self.assertIn(
            "team_slot_index(tslot) == team_number(pplayer->team)", allowed
        )
        self.assertIn("if (team_slot_is_used(tslot))", allowed)
        self.assertIn(
            "player_list_size(team_members(pplayer->team)) <= 1", allowed
        )
        self.assertIn("team_slots_iterate(first_unused)", allowed)
        self.assertIn("if (!team_slot_is_used(first_unused))", allowed)
        self.assertIn("return first_unused == tslot;", allowed)

        guard = (
            "if (caller != nullptr && conn_get_access(caller) == ALLOW_BASIC\n"
            "        && conn_get_player(caller) == pplayer\n"
            "        && !team_command_self_service_allowed(pplayer, tslot))"
        )
        self.assertIn(guard, team_command)
        self.assertLess(
            team_command.index(guard), team_command.index("team_add_player")
        )

        self.assertIn(
            "self_service_team = (cmd == CMD_TEAM\n"
            "                       && team_command_is_self_service(caller, arg));",
            dispatcher,
        )
        self.assertIn("&& !self_service_team\n"
                      "      && !vote_would_pass_immediately", dispatcher)
        self.assertIn("if (caller && !self_service_team", dispatcher)


if __name__ == "__main__":
    unittest.main()
