from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from enhanced_engineer.context import ContextAssembler, ContextRequest
from enhanced_engineer.litesemrag import LiteSemRAG


def card(
    card_id: str,
    title: str,
    text: str,
    *,
    kind: str = "how-to",
    tokens: list[str] | None = None,
    relations: list[str] | None = None,
) -> dict:
    return {
        "id": card_id,
        "documentId": card_id.split("#")[0],
        "chunkId": card_id.split("#")[-1] if "#" in card_id else "summary",
        "kind": kind,
        "locale": "en-US",
        "title": title,
        "text": text,
        "tokens": tokens or [],
        "entities": [],
        "relations": relations or [],
        "helpIds": [],
        "status": "verified",
        "contentHash": "sha256:" + ("1" * 64),
    }


class LiteSemRAGTests(unittest.TestCase):
    def setUp(self):
        self.rag = LiteSemRAG()
        self.cards = [
            card(
                "djehuti.station.track.gain#adjust",
                "Adjust track gain",
                "Change the gain setting on a selected Station track.",
                tokens=["track", "gain", "volume", "station"],
                relations=["djehuti.station.track.select#identify"],
            ),
            card(
                "djehuti.station.track.select#identify",
                "Identify a track",
                "Resolve a track by its name, role, position, selection, and recent use.",
                tokens=["track", "selected", "guitar", "vocal"],
            ),
            card(
                "djehuti.station.transport#record",
                "Record a session",
                "Start and stop the Station recording transport.",
                tokens=["record", "transport", "session"],
            ),
        ]
        self.rag.ingest_cards(self.cards)

    def tearDown(self):
        self.rag.close()

    def test_builds_all_four_core_layers(self):
        stats = self.rag.statistics()
        self.assertEqual(3, stats["cards"])
        self.assertGreaterEqual(stats["layers"]["document"], 3)
        self.assertGreaterEqual(stats["layers"]["chunk"], 3)
        self.assertGreaterEqual(stats["layers"]["semantic"], 3)
        self.assertGreater(stats["layers"]["token"], 3)

    def test_retrieval_uses_authored_tokens_and_graph(self):
        hits = self.rag.retrieve("turn down the guitar track volume", top_k=3)
        self.assertEqual("djehuti.station.track.gain#adjust", hits[0].card_id)
        self.assertIn("djehuti.station.track.select#identify", [hit.card_id for hit in hits])
        self.assertTrue(any("token" in reason for reason in hits[0].reasons))

    def test_ingests_jsonl(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "cards.jsonl"
            source.write_text("\n".join(json.dumps(item) for item in self.cards), encoding="utf-8")
            other = LiteSemRAG(Path(directory) / "index.db")
            try:
                self.assertEqual(3, other.ingest_jsonl(source))
                self.assertEqual(3, other.statistics()["cards"])
            finally:
                other.close()

    def test_recovery_and_cancellation_require_matching_query_intent(self):
        rag = LiteSemRAG()
        try:
            rag.ingest_cards(
                [
                    card("process.replace", "Replace a recording", "Replace one take with a corrected take.", kind="process", tokens=["replace", "recording"]),
                    card("process.replace#recovery", "Replacement failed", "Recover when replacement playback is wrong.", kind="process-recovery", tokens=["replace", "failed", "wrong"]),
                    card("process.replace#cancel", "Cancel replacement", "Stop and cancel the replacement.", kind="process-cancellation", tokens=["replace", "cancel", "stop"]),
                ]
            )
            self.assertEqual("process.replace", rag.retrieve("replace this recording")[0].card_id)
            self.assertEqual("process.replace#recovery", rag.retrieve("replacement failed and is wrong")[0].card_id)
            self.assertEqual("process.replace#cancel", rag.retrieve("stop and cancel replacement")[0].card_id)
        finally:
            rag.close()


class ContextAssemblerTests(unittest.TestCase):
    def setUp(self):
        self.rag = LiteSemRAG()
        self.rag.ingest_cards(
            [card("djehuti.station.track.gain#adjust", "Adjust gain", "Change track gain.", tokens=["gain"])]
        )
        self.policy = {
            "id": "djehuti.policy.confirm-destructive",
            "title": "Confirm destructive changes",
            "policyText": "Confirm destructive changes with the user.",
            "enforcement": "mandatory",
            "priority": 900,
            "scope": {"products": ["DjehutiSuite"]},
            "activation": {
                "mode": "conditional",
                "match": "any",
                "conditions": [{"fact": "riskLevel", "values": ["destructive"]}],
            },
            "status": "verified",
        }
        self.process = {
            "id": "djehuti.process.adjust-track",
            "title": "Adjust a track",
            "purpose": "Safely adjust one track.",
            "requiredPolicies": [self.policy["id"]],
            "steps": [
                {"id": "identify", "instruction": "Identify the track."},
                {"id": "adjust", "instruction": "Adjust it.", "policyIds": [self.policy["id"]]},
            ],
            "verification": {"contentRevision": 2},
        }
        self.assembler = ContextAssembler(
            self.rag,
            {"policies": [self.policy]},
            {"processes": [self.process]},
        )

    def tearDown(self):
        self.rag.close()

    def test_pins_process_policy_before_retrieved_help(self):
        state = {
            "processId": self.process["id"],
            "processRevision": 2,
            "currentStepId": "adjust",
            "status": "active",
        }
        result = self.assembler.assemble(
            ContextRequest(text="lower the gain", product="DjehutiSuite"),
            active_process_state=state,
        )
        sections = result.prompt_sections()
        self.assertEqual("mandatory-policies", sections[0]["kind"])
        self.assertEqual(self.policy["id"], sections[0]["items"][0]["id"])
        self.assertEqual("active-process", sections[1]["kind"])
        self.assertEqual("djehuti.station.track.gain#adjust", result.knowledge[0].card_id)

    def test_missing_active_process_fails_loudly(self):
        with self.assertRaisesRegex(ValueError, "definition is missing"):
            self.assembler.assemble(
                ContextRequest(text="test", product="DjehutiSuite"),
                active_process_state={
                    "processId": "missing.process",
                    "processRevision": 1,
                    "currentStepId": "start",
                    "status": "active",
                },
            )


if __name__ == "__main__":
    unittest.main()
