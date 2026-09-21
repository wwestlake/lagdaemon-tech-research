"""Typed tool registry and authorization boundary for the Enhanced Engineer."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Mapping, Optional

from .access import (
    AccessController,
    AccessDecision,
    AccessSession,
    ApprovalGrant,
    DecisionKind,
    ToolAccess,
    ToolCall,
)


class ToolStatus(str, Enum):
    SUCCESS = "success"
    ERROR = "error"
    APPROVAL_REQUIRED = "approval-required"
    DENIED = "denied"


@dataclass(frozen=True)
class ToolDefinition:
    name: str
    description: str
    parameters: dict[str, Any]
    access: ToolAccess
    usage: str
    tokens: tuple[str, ...] = ()
    target_argument: str | None = None

    def provider_schema(self) -> dict[str, Any]:
        return {
            "type": "function",
            "function": {
                "name": self.name,
                "description": self.description,
                "parameters": self.parameters,
            },
        }


@dataclass(frozen=True)
class ToolOutput:
    message: str
    data: Mapping[str, Any] = field(default_factory=dict)
    changed_paths: tuple[str, ...] = ()


@dataclass(frozen=True)
class ToolInvocation:
    status: ToolStatus
    call: ToolCall
    decision: AccessDecision
    output: ToolOutput | None = None


@dataclass(frozen=True)
class ToolAuditEvent:
    timestamp: str
    tool_name: str
    call_fingerprint: str
    decision: str
    status: str
    reason: str


Handler = Callable[[Mapping[str, Any]], ToolOutput]
TargetResolver = Callable[[Mapping[str, Any]], Optional[Path]]


def _validate_schema(value: Any, schema: Mapping[str, Any], location: str = "arguments") -> None:
    expected = schema.get("type")
    if expected == "object":
        if not isinstance(value, Mapping):
            raise ValueError(f"{location} must be an object")
        properties = schema.get("properties", {})
        for name in schema.get("required", ()):
            if name not in value:
                raise ValueError(f"{location}.{name} is required")
        if schema.get("additionalProperties") is False:
            unknown = sorted(set(value) - set(properties))
            if unknown:
                raise ValueError(f"{location} contains unknown field: {unknown[0]}")
        for name, item in value.items():
            if name in properties:
                _validate_schema(item, properties[name], f"{location}.{name}")
        return
    if expected == "array":
        if not isinstance(value, list):
            raise ValueError(f"{location} must be an array")
        if len(value) < int(schema.get("minItems", 0)):
            raise ValueError(f"{location} has too few items")
        for index, item in enumerate(value):
            _validate_schema(item, schema.get("items", {}), f"{location}[{index}]")
        return
    if expected == "string":
        if not isinstance(value, str):
            raise ValueError(f"{location} must be a string")
        if len(value) < int(schema.get("minLength", 0)):
            raise ValueError(f"{location} is too short")
        return
    if expected == "integer":
        if isinstance(value, bool) or not isinstance(value, int):
            raise ValueError(f"{location} must be an integer")
        if "minimum" in schema and value < int(schema["minimum"]):
            raise ValueError(f"{location} is below its minimum")
        if "maximum" in schema and value > int(schema["maximum"]):
            raise ValueError(f"{location} is above its maximum")
        return
    if expected == "boolean" and not isinstance(value, bool):
        raise ValueError(f"{location} must be a boolean")


class ToolRegistry:
    def __init__(self, controller: AccessController | None = None) -> None:
        self.controller = controller or AccessController()
        self._definitions: dict[str, ToolDefinition] = {}
        self._handlers: dict[str, Handler] = {}
        self._target_resolvers: dict[str, TargetResolver] = {}
        self.audit_events: list[ToolAuditEvent] = []

    def register(
        self,
        definition: ToolDefinition,
        handler: Handler,
        *,
        target_resolver: TargetResolver | None = None,
    ) -> None:
        if definition.name in self._definitions:
            raise ValueError(f"tool is already registered: {definition.name}")
        self._definitions[definition.name] = definition
        self._handlers[definition.name] = handler
        self._target_resolvers[definition.name] = target_resolver or (lambda _: None)

    def definitions(self) -> tuple[ToolDefinition, ...]:
        return tuple(self._definitions[name] for name in sorted(self._definitions))

    def provider_schemas(self) -> list[dict[str, Any]]:
        return [definition.provider_schema() for definition in self.definitions()]

    def prepare_call(self, tool_name: str, arguments: Mapping[str, Any]) -> ToolCall:
        if tool_name not in self._definitions:
            raise KeyError(f"unknown tool: {tool_name}")
        _validate_schema(arguments, self._definitions[tool_name].parameters)
        target = self._target_resolvers[tool_name](arguments)
        return ToolCall(tool_name, dict(arguments), target)

    def invoke(
        self,
        session: AccessSession,
        tool_name: str,
        arguments: Mapping[str, Any],
        *,
        approval: ApprovalGrant | None = None,
    ) -> ToolInvocation:
        try:
            call = self.prepare_call(tool_name, arguments)
        except (KeyError, TypeError, ValueError) as error:
            call = ToolCall(tool_name, dict(arguments))
            decision = AccessDecision(
                DecisionKind.DENY,
                str(error),
                call.fingerprint(),
                session.level,
            )
            return self._finish(ToolStatus.DENIED, call, decision)

        definition = self._definitions[tool_name]
        decision = self.controller.decide(session, definition.access, call, approval)
        if decision.kind == DecisionKind.REQUIRE_APPROVAL:
            return self._finish(ToolStatus.APPROVAL_REQUIRED, call, decision)
        if decision.kind == DecisionKind.DENY:
            return self._finish(ToolStatus.DENIED, call, decision)

        try:
            output = self._handlers[tool_name](arguments)
            return self._finish(ToolStatus.SUCCESS, call, decision, output)
        except (OSError, UnicodeError, ValueError) as error:
            output = ToolOutput(str(error))
            return self._finish(ToolStatus.ERROR, call, decision, output)

    def _finish(
        self,
        status: ToolStatus,
        call: ToolCall,
        decision: AccessDecision,
        output: ToolOutput | None = None,
    ) -> ToolInvocation:
        self.audit_events.append(
            ToolAuditEvent(
                timestamp=datetime.now(timezone.utc).isoformat(),
                tool_name=call.tool_name,
                call_fingerprint=call.fingerprint(),
                decision=decision.kind.value,
                status=status.value,
                reason=decision.reason if output is None else output.message,
            )
        )
        return ToolInvocation(status, call, decision, output)
