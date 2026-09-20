"""Deterministic context assembly for typed and spoken Engineer requests."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

from .litesemrag import LiteSemRAG, RetrievalHit


@dataclass(frozen=True)
class ContextRequest:
    text: str
    product: str
    audience: str | None = None
    platform: str | None = None
    help_ids: tuple[str, ...] = ()
    facts: dict[str, tuple[str, ...]] = field(default_factory=dict)
    conversation: tuple[dict[str, str], ...] = ()


@dataclass(frozen=True)
class AssembledContext:
    mandatory_policies: tuple[dict[str, Any], ...]
    advisory_policies: tuple[dict[str, Any], ...]
    active_process: dict[str, Any] | None
    active_process_state: dict[str, Any] | None
    knowledge: tuple[RetrievalHit, ...]
    conversation: tuple[dict[str, str], ...]
    request: str

    def diagnostics(self) -> dict[str, Any]:
        return {
            "mandatoryPolicyIds": [item["id"] for item in self.mandatory_policies],
            "advisoryPolicyIds": [item["id"] for item in self.advisory_policies],
            "processId": self.active_process.get("id") if self.active_process else None,
            "processStepId": (
                self.active_process_state.get("currentStepId")
                if self.active_process_state else None
            ),
            "knowledgeCardIds": [item.card_id for item in self.knowledge],
            "retrieval": [
                {"id": item.card_id, "score": item.score, "reasons": list(item.reasons)}
                for item in self.knowledge
            ],
        }

    def prompt_sections(self) -> list[dict[str, Any]]:
        """Return ordered structured sections for a provider-specific prompt renderer."""
        return [
            {
                "kind": "mandatory-policies",
                "cannotBeOverridden": True,
                "items": list(self.mandatory_policies),
            },
            {
                "kind": "active-process",
                "definition": self.active_process,
                "state": self.active_process_state,
            },
            {"kind": "advisory-policies", "items": list(self.advisory_policies)},
            {
                "kind": "retrieved-knowledge",
                "items": [
                    {
                        "id": item.card_id,
                        "title": item.title,
                        "text": item.text,
                        "source": item.source,
                    }
                    for item in self.knowledge
                ],
            },
            {"kind": "conversation", "items": list(self.conversation)},
            {"kind": "current-request", "text": self.request},
        ]


def _load_json(value: str | Path | dict[str, Any]) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    return json.loads(Path(value).read_text(encoding="utf-8"))


class ContextAssembler:
    """Pin obligations first, then add a small query-specific knowledge set."""

    def __init__(
        self,
        rag: LiteSemRAG,
        policy_index: str | Path | dict[str, Any],
        process_catalog: str | Path | dict[str, Any],
    ) -> None:
        self.rag = rag
        self.policy_index = _load_json(policy_index)
        self.process_catalog = _load_json(process_catalog)
        self.policies = {
            policy["id"]: policy for policy in self.policy_index.get("policies", [])
        }
        self.processes = {
            process["id"]: process for process in self.process_catalog.get("processes", [])
        }

    @staticmethod
    def _scope_matches(
        policy: dict[str, Any], request: ContextRequest, state: dict[str, Any] | None
    ) -> bool:
        scope = policy.get("scope", {})
        checks = (
            ("products", {request.product}),
            ("audiences", {request.audience} if request.audience else set()),
            ("platforms", {request.platform} if request.platform else set()),
            ("helpIds", set(request.help_ids)),
            (
                "processIds",
                {state["processId"]} if state and state.get("processId") else set(),
            ),
        )
        for key, actual in checks:
            expected = set(scope.get(key, []))
            if expected and not expected.intersection(actual):
                return False
        return True

    @staticmethod
    def _activation_matches(policy: dict[str, Any], facts: dict[str, set[str]]) -> bool:
        activation = policy.get("activation", {"mode": "always"})
        if activation.get("mode") == "always":
            return True
        matches = []
        for condition in activation.get("conditions", []):
            actual = facts.get(condition["fact"], set())
            matched = bool(actual.intersection(condition.get("values", [])))
            matches.append(not matched if condition.get("negate") else matched)
        if not matches:
            return False
        return all(matches) if activation.get("match") == "all" else any(matches)

    def _applicable_policies(
        self,
        request: ContextRequest,
        state: dict[str, Any] | None,
        process: dict[str, Any] | None,
    ) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
        facts = {key: set(values) for key, values in request.facts.items()}
        facts.setdefault("helpId", set()).update(request.help_ids)
        if state and state.get("processId"):
            facts.setdefault("processId", set()).add(state["processId"])
        if state and state.get("currentStepId"):
            facts.setdefault("processStepId", set()).add(state["currentStepId"])
        chosen: dict[str, dict[str, Any]] = {}
        for policy in self.policies.values():
            if policy.get("status") == "deprecated":
                continue
            if self._scope_matches(policy, request, state) and self._activation_matches(policy, facts):
                chosen[policy["id"]] = policy

        if process and state and state.get("status") not in {"completed", "cancelled", "failed"}:
            required = list(process.get("requiredPolicies", []))
            step = next(
                (item for item in process.get("steps", []) if item["id"] == state.get("currentStepId")),
                None,
            )
            required.extend(step.get("policyIds", []) if step else [])
            for policy_id in required:
                if policy_id not in self.policies:
                    raise ValueError(f"active process requires missing policy: {policy_id}")
                chosen[policy_id] = self.policies[policy_id]

        superseded = {
            policy_id
            for policy in chosen.values()
            for policy_id in policy.get("supersedes", [])
            if policy_id in chosen
        }
        for policy_id in superseded:
            chosen.pop(policy_id, None)

        mandatory = [item for item in chosen.values() if item["enforcement"] == "mandatory"]
        advisory = [item for item in chosen.values() if item["enforcement"] == "advisory"]
        key = lambda item: (-int(item.get("priority", 0)), item["id"])
        return sorted(mandatory, key=key), sorted(advisory, key=key)

    def assemble(
        self,
        request: ContextRequest,
        *,
        active_process_state: dict[str, Any] | None = None,
        knowledge_limit: int = 8,
    ) -> AssembledContext:
        process = None
        if active_process_state and active_process_state.get("status") not in {
            "completed", "cancelled", "failed"
        }:
            process_id = active_process_state["processId"]
            process = self.processes.get(process_id)
            if process is None:
                raise ValueError(f"active process definition is missing: {process_id}")
            expected_revision = process.get("verification", {}).get("contentRevision")
            if expected_revision is not None and active_process_state.get("processRevision") != expected_revision:
                raise ValueError(
                    f"active process revision mismatch: state has "
                    f"{active_process_state.get('processRevision')}, definition has {expected_revision}"
                )

        mandatory, advisory = self._applicable_policies(
            request, active_process_state, process
        )
        knowledge = self.rag.retrieve(request.text, top_k=knowledge_limit)
        return AssembledContext(
            mandatory_policies=tuple(mandatory),
            advisory_policies=tuple(advisory),
            active_process=process,
            active_process_state=active_process_state,
            knowledge=tuple(knowledge),
            conversation=request.conversation,
            request=request.text,
        )
