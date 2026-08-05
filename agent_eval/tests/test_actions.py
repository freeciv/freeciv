import unittest

from agent_eval.actions import ActionError, deterministic_action, validate_action


class ActionTests(unittest.TestCase):
    def test_valid_action(self):
        action = validate_action({"type": "set_traits", "traits": {"aggressive": -49, "builder": 0, "expansionist": 50, "trader": 1}})
        self.assertEqual(action["traits"]["expansionist"], 50)

    def test_bool_and_out_of_range_rejected(self):
        for value in (True, 51, -50, 1.5):
            with self.subTest(value=value), self.assertRaises(ActionError):
                validate_action({"type": "set_traits", "traits": {"aggressive": value, "builder": 0, "expansionist": 0, "trader": 0}})

    def test_deterministic_is_stable(self):
        observation = {"num_cities": 2, "num_units": 5, "gold": 80, "turn": 12}
        self.assertEqual(deterministic_action(observation), deterministic_action(observation))


if __name__ == "__main__":
    unittest.main()
