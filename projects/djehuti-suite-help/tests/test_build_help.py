import argparse
import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import build_help


class HelpBuildTests(unittest.TestCase):
    def test_example_build_produces_both_consumers(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = build_help.build(
                argparse.Namespace(
                    topics=ROOT / "examples" / "topics",
                    inventory=ROOT / "examples" / "help-inventory.json",
                    topic_schema=ROOT / "schemas" / "help-topic.schema.json",
                    inventory_schema=ROOT / "schemas" / "help-inventory.schema.json",
                    output=output,
                )
            )

            self.assertEqual(0, result)
            self.assertTrue((output / "help-catalog.json").exists())
            self.assertTrue((output / "context-map.json").exists())
            cards = (output / "semantic-cards.jsonl").read_text(encoding="utf-8").splitlines()
            self.assertGreaterEqual(len(cards), 2)
            report = json.loads((output / "sync-report.json").read_text(encoding="utf-8"))
            self.assertTrue(report["ok"])

    def test_behavior_change_requires_help_review(self):
        topic = build_help.load_json(
            ROOT / "examples" / "topics" / "example.djehuti.help.context-help.json"
        )
        inventory = build_help.load_json(ROOT / "examples" / "help-inventory.json")
        changed = copy.deepcopy(inventory)
        changed["items"][0]["behaviorSignature"] = "sha256:" + ("f" * 64)

        errors, _ = build_help.validate_links([topic], changed)

        self.assertTrue(any("stale behavior signature" in error for error in errors))

    def test_required_context_must_be_covered(self):
        inventory = build_help.load_json(ROOT / "examples" / "help-inventory.json")

        errors, _ = build_help.validate_links([], inventory)

        self.assertTrue(any("required helpId is not covered" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
