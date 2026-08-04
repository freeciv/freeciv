from __future__ import annotations

from pathlib import Path
import unittest

import agent_eval.v2_control as v2_control
from agent_eval.tests.test_v2_control import (
    _action,
    citizen_control_rows,
    governor_goal,
    observation,
    scoped_city_rows,
    scoped_government_rows,
    valid_rows,
)
from agent_eval.v2_control import V2ActorScopeRequest, V2ControlError, V2SeatControl


GOVERNOR_ROW = (
    "city_governor city=c:20:200 min_food=0 min_production=0 "
    "min_trade=0 min_gold=0 min_luxury=0 min_science=0 "
    "weight_food=1 weight_production=1 weight_trade=1 "
    "weight_gold=1 weight_luxury=1 weight_science=1 "
    "celebration_weight=1 require_happy=0 maximize_growth=0"
)


def _parsed_action(control: V2SeatControl, row: str):
    return control._parse_row(
        "action", dict(token.split("=", 1) for token in row.split()[1:]),
    )


class V2RemainingControlTests(unittest.TestCase):
    @staticmethod
    def _actor_catalog(request: V2ActorScopeRequest, rows: tuple[str, ...]):
        return {
            "generation": 1,
            "native_revision": request.native_revision,
            "actor_ref": request.native_actor_ref,
            "view_id": f"v{request.native_revision}-301",
            "offset": 0,
            "count": len(rows),
            "total_count": len(rows),
            "next_offset": len(rows),
            "complete": True,
            "overflow": False,
            "rows": rows,
        }

    def test_native_action_equality_includes_control_arguments(self):
        source = (
            Path(__file__).parents[2]
            / "client"
            / "gui-agent"
            / "protocol_v2.c"
        ).read_text(encoding="utf-8")
        body = source.split("static bool v2_action_equal", 1)[1].split(
            "static bool v2_current_equal", 1,
        )[0]
        for field in (
            "spaceship_part",
            "spaceship_value",
            "target_multiplier",
            "multiplier_value",
        ):
            with self.subTest(field=field):
                self.assertIn(f"a->{field} == b->{field}", body)

    def test_multiplier_state_and_action_are_opaque_and_exact(self):
        rows = tuple(
            row.replace("can_change=0 choice_count=11", "can_change=1 choice_count=11")
            if row.startswith("multiplier ") else row
            for row in valid_rows()
        )
        control = V2SeatControl("game_multiplier", "agent_multiplier", 1)
        snapshot = control._snapshot(observation(rows))
        multiplier = snapshot.sections["multipliers"][0]
        self.assertNotEqual(multiplier["id"], "0")
        self.assertTrue(multiplier["can_change"])
        actor_id = next(
            public_id for public_id, binding in snapshot.actor_bindings.items()
            if binding.kind == "player"
        )
        request = V2ActorScopeRequest(
            actor_id, "player", "p:1:10", snapshot.native_revision, 16,
        )
        action = _parsed_action(control, _action(
            300, "player.set_multiplier", "p:1:10", -1,
            "player.set_multiplier", "Multiplier", "Multiplier Target Changed", 0,
            target_multiplier=0, multiplier_value=60, target_name="Policy",
        ))
        descriptor, binding = control._project_scoped_action(
            snapshot, request, action, "action_multiplier",
        )
        self.assertEqual(descriptor["subject"]["target"]["value"], 60)
        self.assertEqual(binding.operation, "set_multiplier")

    def test_spaceship_state_and_structural_action_are_exact(self):
        rows = []
        for row in valid_rows():
            if row.startswith("spaceship state="):
                row = row.replace(
                    "state=none structurals=0", "state=started structurals=1",
                )
            elif row.startswith("spaceship_structural slot=0 "):
                row = row.replace("can_place=0", "can_place=1")
            rows.append(row)
        control = V2SeatControl("game_ship", "agent_ship", 1)
        snapshot = control._snapshot(observation(tuple(sorted(rows))))
        ship = snapshot.sections["spaceship"][0]
        self.assertEqual(ship["state"], "started")
        self.assertTrue(ship["structural_slots"][0]["can_place"])
        actor_id = next(
            public_id for public_id, binding in snapshot.actor_bindings.items()
            if binding.kind == "player"
        )
        request = V2ActorScopeRequest(
            actor_id, "player", "p:1:10", snapshot.native_revision, 16,
        )
        action = _parsed_action(control, _action(
            301, "spaceship.place_component", "p:1:10", -1,
            "spaceship.place_component", "Spaceship Part",
            "Spaceship Part Placed", 0, spaceship_part="structural",
            spaceship_value=0, target_name="structural",
        ))
        descriptor, binding = control._project_scoped_action(
            snapshot, request, action, "action_spaceship",
        )
        self.assertEqual(descriptor["subject"]["target"]["part"], "structural")
        self.assertEqual(binding.operation, "place_component")

    def test_player_scope_is_complete_for_multiplier_and_spaceship(self):
        rows = []
        for row in valid_rows():
            if row.startswith("multiplier "):
                row = row.replace(
                    "start=0 stop=100 step=10 minimum_turns=2 changed_turn=0 "
                    "can_change=0 choice_count=11",
                    "start=0 stop=50 step=50 minimum_turns=2 changed_turn=0 "
                    "can_change=1 choice_count=2",
                )
            elif row.startswith("spaceship state="):
                row = row.replace(
                    "state=none structurals=0", "state=started structurals=1",
                )
            elif row.startswith("spaceship_structural slot=0 "):
                row = row.replace("can_place=0", "can_place=1")
            rows.append(row)
        current = observation(tuple(sorted(rows)))
        control = V2SeatControl("game_player_controls", "agent_controls", 1)
        player_id = control.state_page(current)["page"]["items"][0]["player"][
            "id"
        ]
        native = tuple(
            row for row in rows
            if row.startswith("action ") and " actor=none " in row
        ) + scoped_government_rows() + (
            _action(
                300, "player.set_multiplier", "p:1:10", -1,
                "player.set_multiplier", "Multiplier",
                "Multiplier Target Changed", 0,
                target_multiplier=0, multiplier_value=0,
                target_name="Policy",
            ),
            _action(
                301, "spaceship.place_component", "p:1:10", -1,
                "spaceship.place_component", "Spaceship Part",
                "Spaceship Part Placed", 0,
                spaceship_part="structural", spaceship_value=0,
                target_name="structural",
            ),
        )
        request = control.prepare_actor_scope(current, player_id, 16)
        page = control.materialize_actor_scope(
            request, self._actor_catalog(request, native),
        )
        self.assertEqual(page["page"]["total_items"], len(native))
        by_operation = {
            item["subject"]["operation"]: item
            for item in page["page"]["items"]
        }
        multiplier = control.resolve_action(
            current,
            by_operation["set_multiplier"]["state_revision"],
            by_operation["set_multiplier"]["action_id"],
            {},
        )
        self.assertEqual(multiplier.operation, "set_multiplier")
        self.assertEqual(multiplier.native_arguments, "-")
        spaceship = control.resolve_action(
            current,
            by_operation["place_component"]["state_revision"],
            by_operation["place_component"]["action_id"],
            {},
        )
        self.assertEqual(spaceship.operation, "place_component")
        self.assertEqual(spaceship.native_arguments, "-")

    def test_governor_goal_is_bounded_city_state_and_controls_citizens(self):
        citizen_rows, _direct_citizen_actions = citizen_control_rows()
        rows = tuple(sorted(
            row.replace("governor_enabled=0", "governor_enabled=1")
            if row.startswith("city ref=c:20:200 ") else row
            for row in citizen_rows
        ))
        control = V2SeatControl("game_governor", "agent_governor", 1)
        snapshot = control._snapshot(observation(rows))
        actor_id = next(
            public_id for public_id, binding in snapshot.actor_bindings.items()
            if binding.kind == "city"
        )
        scope = control.prepare_state_scope(
            observation(rows), "city_governor", actor_id=actor_id,
        )
        page = control.materialize_state_scope(scope, {
            "generation": 1,
            "native_revision": snapshot.native_revision,
            "section": "city_governor",
            "selector": "c:20:200",
            "view_id": f"q{snapshot.native_revision}-1",
            "offset": 0,
            "count": 1,
            "total_count": 1,
            "next_offset": 1,
            "complete": True,
            "overflow": False,
            "rows": (GOVERNOR_ROW,),
        })
        self.assertEqual(page["page"]["items"][0]["city_id"], actor_id)
        request = control.prepare_actor_scope(observation(rows), actor_id, 16)
        clear = _parsed_action(control, _action(
            302, "city.clear_governor", "c:20:200", -1,
            "city.clear_governor", "City", "Governor Cleared", 0,
            target_name="governor",
        ))
        descriptor, clear_binding = control._project_scoped_action(
            snapshot, request, clear, "action_clear_governor",
        )
        self.assertEqual(descriptor["subject"]["operation"], "clear_governor")
        self.assertEqual(control._resolve_arguments(snapshot, clear_binding, {}), "-")

        set_action = _parsed_action(control, _action(
            303, "city.set_governor", "c:20:200", -1,
            "city.set_governor", "City", "Governor Goal Set", 0,
            "governor-goal-required", target_name="governor",
        ))
        _, set_binding = control._project_scoped_action(
            snapshot, request, set_action, "action_set_governor",
        )
        current = page["page"]["items"][0]
        goal = {
            "minimum_surplus": current["minimum_surplus"],
            "weights": current["weights"],
            "celebration_weight": current["celebration_weight"],
            "require_happy": current["require_happy"],
            "maximize_growth": current["maximize_growth"],
        }
        with self.assertRaises(V2ControlError):
            control._resolve_arguments(snapshot, set_binding, goal)
        goal["weights"] = {**goal["weights"], "science": 2}
        native = control._resolve_arguments(snapshot, set_binding, goal)
        self.assertIn("weight_science=2", native)

        scoped_rows = scoped_city_rows() + (_action(
            198, "city.clear_governor", "c:20:200", -1,
            "city.clear_governor", "City", "Governor Cleared", 0,
            target_name="governor",
        ),)
        catalog = control.materialize_actor_scope(
            request, self._actor_catalog(request, scoped_rows),
        )
        operations = {
            item["subject"]["operation"] for item in catalog["page"]["items"]
        }
        self.assertEqual(operations, {
            "set_production", "buy_production", "set_worklist",
            "set_options", "rename", "set_governor", "clear_governor",
        })
        self.assertTrue({
            "work_tile", "unwork_tile", "set_specialist",
        }.isdisjoint(operations))
        public_set = next(
            item for item in catalog["page"]["items"]
            if item["subject"]["operation"] == "set_governor"
        )
        resolved_set = control.resolve_action(
            observation(rows), public_set["state_revision"],
            public_set["action_id"], governor_goal(science_weight=2),
        )
        self.assertIn("weight_science=2", resolved_set.native_arguments)
        public_clear = next(
            item for item in catalog["page"]["items"]
            if item["subject"]["operation"] == "clear_governor"
        )
        resolved_clear = control.resolve_action(
            observation(rows), public_clear["state_revision"],
            public_clear["action_id"], {},
        )
        self.assertEqual(resolved_clear.native_arguments, "-")


if __name__ == "__main__":
    unittest.main()

