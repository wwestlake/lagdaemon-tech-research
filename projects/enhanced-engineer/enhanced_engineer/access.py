"""Deterministic access and one-call approval control for Engineer tools."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from enum import Enum, IntEnum
from pathlib import Path
from typing import Any, Callable, Mapping


class AccessLevel(IntEnum):
    OBSERVE = 10
    WORKSPACE = 20
    ENGINEER = 30
    SYSTEM = 40


class RiskClass(str, Enum):
    READ = "read"
    WRITE = "write"
    EXECUTE = "execute"
    EXTERNAL = "external"
    DESTRUCTIVE = "destructive"
    PRIVILEGED = "privileged"


class ApprovalPolicy(str, Enum):
    NEVER = "never"
    ON_ESCALATION = "on-escalation"
    ALWAYS_FOR_ACTIONS = "always-for-actions"


class DecisionKind(str, Enum):
    ALLOW = "allow"
    REQUIRE_APPROVAL = "require-approval"
    DENY = "deny"


@dataclass(frozen=True)
class ToolAccess:
    name: str
    minimum_level: AccessLevel
    risk: RiskClass
    workspace_scoped: bool = False
    always_requires_approval: bool = False
    forbidden: bool = False


@dataclass(frozen=True)
class ToolCall:
    tool_name: str
    arguments: Mapping[str, Any] = field(default_factory=dict)
    target_path: Path | None = None

    def fingerprint(self) -> str:
        target = str(self.target_path.resolve(strict=False)) if self.target_path else None
        payload = json.dumps(
            {
                "tool": self.tool_name,
                "arguments": self.arguments,
                "target": target,
            },
            sort_keys=True,
            separators=(",", ":"),
            default=str,
        )
        return "sha256:" + hashlib.sha256(payload.encode("utf-8")).hexdigest()


@dataclass
class ApprovalGrant:
    call_fingerprint: str
    approved_by: str
    expires_at: datetime
    used: bool = False

    @classmethod
    def for_call(
        cls,
        call: ToolCall,
        *,
        approved_by: str,
        lifetime: timedelta = timedelta(minutes=5),
        now: datetime | None = None,
    ) -> "ApprovalGrant":
        issued_at = now or datetime.now(timezone.utc)
        return cls(call.fingerprint(), approved_by, issued_at + lifetime)

    def consume(self, call: ToolCall, now: datetime) -> bool:
        if self.used or now >= self.expires_at or self.call_fingerprint != call.fingerprint():
            return False
        self.used = True
        return True


@dataclass(frozen=True)
class AccessSession:
    level: AccessLevel
    maximum_level: AccessLevel
    approval_policy: ApprovalPolicy
    workspace_roots: tuple[Path, ...] = ()

    def __post_init__(self) -> None:
        if self.maximum_level < self.level:
            raise ValueError("maximum access level cannot be below the standing level")


@dataclass(frozen=True)
class AccessDecision:
    kind: DecisionKind
    reason: str
    call_fingerprint: str
    effective_level: AccessLevel


class AccessController:
    """Makes authorization decisions without consulting the language model."""

    def __init__(self, clock: Callable[[], datetime] | None = None) -> None:
        self._clock = clock or (lambda: datetime.now(timezone.utc))

    @staticmethod
    def _inside_workspace(path: Path, roots: tuple[Path, ...]) -> bool:
        candidate = path.resolve(strict=False)
        for root in roots:
            resolved_root = root.resolve(strict=False)
            if candidate == resolved_root or resolved_root in candidate.parents:
                return True
        return False

    def decide(
        self,
        session: AccessSession,
        tool: ToolAccess,
        call: ToolCall,
        approval: ApprovalGrant | None = None,
    ) -> AccessDecision:
        fingerprint = call.fingerprint()
        if call.tool_name != tool.name:
            return AccessDecision(
                DecisionKind.DENY,
                "tool declaration does not match the requested call",
                fingerprint,
                session.level,
            )
        if tool.forbidden:
            return AccessDecision(
                DecisionKind.DENY,
                "this capability is forbidden by the host",
                fingerprint,
                session.level,
            )

        required_level = tool.minimum_level
        if tool.workspace_scoped:
            if call.target_path is None:
                return AccessDecision(
                    DecisionKind.DENY,
                    "a workspace-scoped tool requires an explicit target path",
                    fingerprint,
                    session.level,
                )
            if not self._inside_workspace(call.target_path, session.workspace_roots):
                required_level = max(required_level, AccessLevel.SYSTEM)

        if required_level > session.maximum_level:
            return AccessDecision(
                DecisionKind.DENY,
                "the call exceeds the maximum access allowed for this session",
                fingerprint,
                session.level,
            )

        needs_approval = required_level > session.level
        needs_approval = needs_approval or tool.always_requires_approval
        needs_approval = needs_approval or tool.risk in {
            RiskClass.DESTRUCTIVE,
            RiskClass.PRIVILEGED,
        }
        if session.approval_policy == ApprovalPolicy.ALWAYS_FOR_ACTIONS:
            needs_approval = needs_approval or tool.risk != RiskClass.READ

        if not needs_approval:
            return AccessDecision(
                DecisionKind.ALLOW,
                "allowed by the standing session access",
                fingerprint,
                required_level,
            )

        if approval is not None and approval.consume(call, self._clock()):
            return AccessDecision(
                DecisionKind.ALLOW,
                "allowed by an exact, single-use approval",
                fingerprint,
                required_level,
            )

        if session.approval_policy == ApprovalPolicy.NEVER:
            return AccessDecision(
                DecisionKind.DENY,
                "approval is required but this session cannot request it",
                fingerprint,
                required_level,
            )

        return AccessDecision(
            DecisionKind.REQUIRE_APPROVAL,
            "explicit approval is required for this exact call",
            fingerprint,
            required_level,
        )
