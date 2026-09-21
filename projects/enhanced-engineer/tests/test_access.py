from __future__ import annotations

import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from enhanced_engineer.access import (
    AccessController,
    AccessLevel,
    AccessSession,
    ApprovalGrant,
    ApprovalPolicy,
    DecisionKind,
    RiskClass,
    ToolAccess,
    ToolCall,
)


class AccessControllerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.now = datetime(2026, 9, 21, 12, 0, tzinfo=timezone.utc)
        self.controller = AccessController(lambda: self.now)
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def session(
        self,
        level: AccessLevel,
        maximum: AccessLevel | None = None,
        policy: ApprovalPolicy = ApprovalPolicy.ON_ESCALATION,
    ) -> AccessSession:
        return AccessSession(level, maximum or level, policy, (self.root,))

    def test_observe_allows_reads_but_not_workspace_writes(self) -> None:
        read = ToolAccess("read_file", AccessLevel.OBSERVE, RiskClass.READ, True)
        write = ToolAccess("write_file", AccessLevel.WORKSPACE, RiskClass.WRITE, True)
        target = self.root / "main.fr"
        self.assertEqual(
            DecisionKind.ALLOW,
            self.controller.decide(self.session(AccessLevel.OBSERVE), read, ToolCall("read_file", target_path=target)).kind,
        )
        self.assertEqual(
            DecisionKind.DENY,
            self.controller.decide(self.session(AccessLevel.OBSERVE), write, ToolCall("write_file", target_path=target)).kind,
        )

    def test_workspace_write_is_allowed_inside_the_root(self) -> None:
        tool = ToolAccess("write_file", AccessLevel.WORKSPACE, RiskClass.WRITE, True)
        decision = self.controller.decide(
            self.session(AccessLevel.WORKSPACE),
            tool,
            ToolCall("write_file", {"content": "fn main() {}"}, self.root / "src" / "main.fr"),
        )
        self.assertEqual(DecisionKind.ALLOW, decision.kind)

    def test_outside_workspace_requires_system_escalation(self) -> None:
        tool = ToolAccess("write_file", AccessLevel.WORKSPACE, RiskClass.WRITE, True)
        call = ToolCall("write_file", target_path=self.root.parent / "outside.fr")
        session = self.session(AccessLevel.WORKSPACE, AccessLevel.SYSTEM)
        self.assertEqual(DecisionKind.REQUIRE_APPROVAL, self.controller.decide(session, tool, call).kind)

    def test_exact_approval_is_single_use_and_argument_bound(self) -> None:
        tool = ToolAccess("delete_file", AccessLevel.WORKSPACE, RiskClass.DESTRUCTIVE, True)
        call = ToolCall("delete_file", {"recursive": False}, self.root / "old.fr")
        other = ToolCall("delete_file", {"recursive": True}, self.root / "old.fr")
        session = self.session(AccessLevel.WORKSPACE, AccessLevel.WORKSPACE)
        grant = ApprovalGrant.for_call(call, approved_by="user", now=self.now)

        self.assertEqual(
            DecisionKind.REQUIRE_APPROVAL,
            self.controller.decide(session, tool, other, grant).kind,
        )
        self.assertFalse(grant.used)
        self.assertEqual(DecisionKind.ALLOW, self.controller.decide(session, tool, call, grant).kind)
        self.assertTrue(grant.used)
        self.assertEqual(
            DecisionKind.REQUIRE_APPROVAL,
            self.controller.decide(session, tool, call, grant).kind,
        )

    def test_expired_approval_is_rejected(self) -> None:
        tool = ToolAccess("run_build", AccessLevel.ENGINEER, RiskClass.EXECUTE)
        call = ToolCall("run_build", {"target": "check"})
        session = self.session(AccessLevel.WORKSPACE, AccessLevel.ENGINEER)
        grant = ApprovalGrant.for_call(
            call,
            approved_by="user",
            lifetime=timedelta(seconds=1),
            now=self.now - timedelta(minutes=1),
        )
        self.assertEqual(
            DecisionKind.REQUIRE_APPROVAL,
            self.controller.decide(session, tool, call, grant).kind,
        )

    def test_forbidden_tool_cannot_be_approved(self) -> None:
        tool = ToolAccess(
            "disable_security", AccessLevel.SYSTEM, RiskClass.PRIVILEGED, forbidden=True
        )
        call = ToolCall("disable_security")
        grant = ApprovalGrant.for_call(call, approved_by="user", now=self.now)
        decision = self.controller.decide(
            self.session(AccessLevel.SYSTEM, AccessLevel.SYSTEM), tool, call, grant
        )
        self.assertEqual(DecisionKind.DENY, decision.kind)
        self.assertFalse(grant.used)

    def test_never_policy_denies_instead_of_prompting(self) -> None:
        tool = ToolAccess("run_build", AccessLevel.ENGINEER, RiskClass.EXECUTE)
        call = ToolCall("run_build")
        decision = self.controller.decide(
            self.session(AccessLevel.WORKSPACE, AccessLevel.ENGINEER, ApprovalPolicy.NEVER),
            tool,
            call,
        )
        self.assertEqual(DecisionKind.DENY, decision.kind)


if __name__ == "__main__":
    unittest.main()
