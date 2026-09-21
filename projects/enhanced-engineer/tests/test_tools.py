from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from enhanced_engineer.access import (
    AccessLevel,
    AccessSession,
    ApprovalGrant,
    ApprovalPolicy,
)
from enhanced_engineer.tool_cards import tool_cards
from enhanced_engineer.litesemrag import LiteSemRAG
from enhanced_engineer.tools import ToolStatus
from enhanced_engineer.workspace_tools import create_engineering_registry


class EngineeringToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.registry = create_engineering_registry(
            self.root,
            allowed_executables=(Path(sys.executable).name,),
        )
        self.observe = AccessSession(
            AccessLevel.OBSERVE,
            AccessLevel.OBSERVE,
            ApprovalPolicy.ON_ESCALATION,
            (self.root,),
        )
        self.workspace = AccessSession(
            AccessLevel.WORKSPACE,
            AccessLevel.WORKSPACE,
            ApprovalPolicy.ON_ESCALATION,
            (self.root,),
        )
        self.engineer = AccessSession(
            AccessLevel.ENGINEER,
            AccessLevel.ENGINEER,
            ApprovalPolicy.ON_ESCALATION,
            (self.root,),
        )

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_creates_project_files_then_reads_and_searches_them(self) -> None:
        created = self.registry.invoke(
            self.workspace,
            "workspace_create_file",
            {"path": "demo/src/main.fr", "content": "fn main() {\n    print(42);\n}\n"},
        )
        self.assertEqual(ToolStatus.SUCCESS, created.status)
        self.assertTrue((self.root / "demo" / "src" / "main.fr").is_file())

        read = self.registry.invoke(
            self.observe,
            "workspace_read",
            {"path": "demo/src/main.fr", "start_line": 2, "end_line": 2},
        )
        self.assertEqual(ToolStatus.SUCCESS, read.status)
        self.assertIn("print(42)", read.output.data["text"])

        found = self.registry.invoke(
            self.observe,
            "workspace_search",
            {"query": "print", "path": "demo", "file_pattern": "*.fr"},
        )
        self.assertEqual(ToolStatus.SUCCESS, found.status)
        self.assertEqual("demo/src/main.fr", found.output.data["matches"][0]["path"])

    def test_create_never_overwrites_and_exact_replace_rejects_ambiguity(self) -> None:
        file = self.root / "notes.md"
        file.write_text("same\nsame\n", encoding="utf-8")
        create = self.registry.invoke(
            self.workspace,
            "workspace_create_file",
            {"path": "notes.md", "content": "lost"},
        )
        self.assertEqual(ToolStatus.ERROR, create.status)
        self.assertEqual("same\nsame\n", file.read_text(encoding="utf-8"))

        replace = self.registry.invoke(
            self.workspace,
            "workspace_replace_text",
            {"path": "notes.md", "old_text": "same", "new_text": "changed"},
        )
        self.assertEqual(ToolStatus.ERROR, replace.status)

    def test_registry_rejects_missing_wrong_and_unknown_arguments(self) -> None:
        missing = self.registry.invoke(self.observe, "workspace_read", {})
        wrong_type = self.registry.invoke(
            self.observe,
            "workspace_read",
            {"path": "notes.md", "start_line": "one"},
        )
        unknown = self.registry.invoke(
            self.workspace,
            "workspace_create_file",
            {"path": "notes.md", "content": "text", "overwrite": True},
        )
        self.assertEqual(ToolStatus.DENIED, missing.status)
        self.assertEqual(ToolStatus.DENIED, wrong_type.status)
        self.assertEqual(ToolStatus.DENIED, unknown.status)
        self.assertFalse((self.root / "notes.md").exists())

    def test_full_overwrite_requires_exact_single_use_approval(self) -> None:
        file = self.root / "spec.md"
        file.write_text("old", encoding="utf-8")
        arguments = {"path": "spec.md", "content": "new"}
        pending = self.registry.invoke(self.workspace, "workspace_overwrite_file", arguments)
        self.assertEqual(ToolStatus.APPROVAL_REQUIRED, pending.status)
        grant = ApprovalGrant.for_call(pending.call, approved_by="user")
        allowed = self.registry.invoke(
            self.workspace,
            "workspace_overwrite_file",
            arguments,
            approval=grant,
        )
        self.assertEqual(ToolStatus.SUCCESS, allowed.status)
        self.assertEqual("new", file.read_text(encoding="utf-8"))
        replay = self.registry.invoke(
            self.workspace,
            "workspace_overwrite_file",
            arguments,
            approval=grant,
        )
        self.assertEqual(ToolStatus.APPROVAL_REQUIRED, replay.status)

    def test_workspace_session_cannot_escape_its_root(self) -> None:
        outside = self.root.parent / "outside-engineer-test.txt"
        result = self.registry.invoke(
            self.workspace,
            "workspace_create_file",
            {"path": str(outside), "content": "no"},
        )
        self.assertEqual(ToolStatus.DENIED, result.status)
        self.assertFalse(outside.exists())

    def test_system_session_can_report_an_approved_path_outside_the_project(self) -> None:
        project = self.root / "project"
        outside = self.root / "outside"
        project.mkdir()
        outside.mkdir()
        registry = create_engineering_registry(project, allowed_executables=())
        system = AccessSession(
            AccessLevel.SYSTEM,
            AccessLevel.SYSTEM,
            ApprovalPolicy.ON_ESCALATION,
            (project,),
        )
        result = registry.invoke(
            system,
            "workspace_create_file",
            {"path": str(outside / "notes.md"), "content": "system scope"},
        )
        self.assertEqual(ToolStatus.SUCCESS, result.status)
        self.assertEqual((str(outside / "notes.md"),), result.output.changed_paths)

    def test_engineer_process_is_allowlisted_and_captures_diagnostics(self) -> None:
        result = self.registry.invoke(
            self.engineer,
            "engineer_run_process",
            {
                "command": [sys.executable, "-c", "print('diagnostic output')"],
                "cwd": ".",
                "timeout_seconds": 10,
            },
        )
        self.assertEqual(ToolStatus.SUCCESS, result.status)
        self.assertEqual(0, result.output.data["exit_code"])
        self.assertIn("diagnostic output", result.output.data["stdout"])

    def test_training_cards_are_generated_from_every_registered_tool(self) -> None:
        definitions = self.registry.definitions()
        cards = tool_cards(definitions)
        self.assertEqual(len(definitions), len(cards))
        self.assertEqual(
            {definition.name for definition in definitions},
            {card["text"].splitlines()[0].removeprefix("Tool: ") for card in cards},
        )
        self.assertTrue(all(card["contentHash"].startswith("sha256:") for card in cards))

    def test_litesemrag_selects_creation_and_diagnostic_tools(self) -> None:
        rag = LiteSemRAG()
        try:
            rag.ingest_cards(tool_cards(self.registry.definitions()))
            creation = rag.retrieve("create a new Frust project and write source files", top_k=3)
            diagnostics = rag.retrieve("run the compiler and inspect diagnostics", top_k=3)
            self.assertEqual(
                "enhanced-engineer.tool.workspace.create.file#usage",
                creation[0].card_id,
            )
            self.assertEqual(
                "enhanced-engineer.tool.engineer.run.process#usage",
                diagnostics[0].card_id,
            )
        finally:
            rag.close()


if __name__ == "__main__":
    unittest.main()
