#!/usr/bin/env python3
"""Policies, processes and active process state for the DjehutiSuite agent context system.

Three responsibilities, all technology independent:

* validate authored policy and process definitions (cross-references, step relationships);
* compile them into read models and LiteSemRAG cards;
* a reference implementation of per-request context assembly and of active-process-state
  validation, so the rules in docs/ARCHITECTURE.md are executable and testable.

Authored definitions are canonical and live in Git. Active process state is runtime data: it is
validated here but never compiled into any authored read model and never read from an authored
source directory.
"""

from __future__ import annotations

from typing import Any, Iterable

import build_help as bh

# --------------------------------------------------------------------------------------
# Authored-definition validation
# --------------------------------------------------------------------------------------

RUNTIME_MARKERS = ("active-process-state",)


def runtime_data_errors(documents: Iterable[tuple[str, Any]]) -> list[str]:
    """Authored sources must never contain runtime records."""
    errors: list[str] = []
    for label, document in documents:
        if isinstance(document, dict) and (
            document.get("runtimeData") is True or document.get("recordKind") in RUNTIME_MARKERS
        ):
            errors.append(
                f"{label}: runtime data (active process state) must not be stored in the authored knowledge source"
            )
    return errors


def validate_definitions(
    policies: list[dict[str, Any]],
    processes: list[dict[str, Any]],
    inventory: dict[str, Any] | None = None,
) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    help_ids = {item["helpId"] for item in inventory["items"]} if inventory else None

    policy_by_id = {policy["id"]: policy for policy in policies}
    process_by_id = {process["id"]: process for process in processes}

    errors.extend(f"duplicate policy id: {item}" for item in bh.duplicate_values(p["id"] for p in policies))
    errors.extend(f"duplicate process id: {item}" for item in bh.duplicate_values(p["id"] for p in processes))
    errors.extend(
        f"id used by both a policy and a process: {item}" for item in sorted(set(policy_by_id) & set(process_by_id))
    )

    def check_help_ids(owner: str, ids: Iterable[str]) -> None:
        if help_ids is None:
            return
        for help_id in ids:
            if help_id not in help_ids:
                errors.append(f"{owner}: helpId not in inventory: {help_id}")

    for policy in policies:
        pid = policy["id"]
        if pid in policy.get("supersedes", []):
            errors.append(f"{pid}: policy supersedes itself")
        for other_id in policy.get("supersedes", []):
            other = policy_by_id.get(other_id)
            if other is None:
                errors.append(f"{pid}: supersedes unknown policy: {other_id}")
            elif other["priority"] > policy["priority"]:
                errors.append(f"{pid}: cannot supersede higher-priority policy {other_id}")
            elif other["enforcement"] == "mandatory" and policy["enforcement"] != "mandatory":
                errors.append(f"{pid}: an advisory policy cannot supersede mandatory policy {other_id}")
        if replacement := policy.get("replacedBy"):
            if replacement not in policy_by_id:
                errors.append(f"{pid}: replacedBy policy does not exist: {replacement}")
        for process_id in policy["scope"].get("processIds", []):
            if process_id not in process_by_id:
                errors.append(f"{pid}: scope refers to unknown process: {process_id}")
        check_help_ids(pid, policy["scope"].get("helpIds", []))
        if policy["enforcement"] == "mandatory" and policy["status"] in ("proposed", "draft"):
            warnings.append(f"{pid}: mandatory policy is not verified (status {policy['status']})")

    for process in processes:
        pid = process["id"]
        steps = process["steps"]
        step_ids = [step["id"] for step in steps]
        step_id_set = set(step_ids)
        recovery_by_id = {path["id"]: path for path in process["recoveryPaths"]}

        errors.extend(f"{pid}: duplicate step id: {item}" for item in bh.duplicate_values(step_ids))
        errors.extend(
            f"{pid}: duplicate recovery path id: {item}" for item in bh.duplicate_values(recovery_by_id_ids(process))
        )
        errors.extend(
            f"{pid}: duplicate completion condition id: {item}"
            for item in bh.duplicate_values(c["id"] for c in process["completionConditions"])
        )
        errors.extend(
            f"{pid}: duplicate cancellation id: {item}"
            for item in bh.duplicate_values(c["id"] for c in process["cancellation"])
        )
        errors.extend(
            f"{pid}: duplicate entry condition id: {item}"
            for item in bh.duplicate_values(c["id"] for c in process["entryConditions"])
        )

        if replacement := process.get("replacedBy"):
            if replacement not in process_by_id:
                errors.append(f"{pid}: replacedBy process does not exist: {replacement}")

        for policy_id in process["requiredPolicies"]:
            policy = policy_by_id.get(policy_id)
            if policy is None:
                errors.append(f"{pid}: required policy does not exist: {policy_id}")
            elif policy["status"] == "deprecated":
                errors.append(f"{pid}: required policy is deprecated: {policy_id}")
            elif process["status"] == "verified" and policy["status"] != "verified":
                warnings.append(f"{pid}: verified process requires unverified policy {policy_id}")

        check_help_ids(pid, process["applicability"].get("helpIds", []))

        for step in steps:
            for policy_id in step.get("policyIds", []):
                if policy_id not in policy_by_id:
                    errors.append(f"{pid}#{step['id']}: step policy does not exist: {policy_id}")
                elif policy_by_id[policy_id]["status"] == "deprecated":
                    errors.append(f"{pid}#{step['id']}: step policy is deprecated: {policy_id}")
            if target := step.get("uiTarget"):
                check_help_ids(f"{pid}#{step['id']}", [target])
            if failure := step.get("onFailure"):
                path = recovery_by_id.get(failure)
                if path is None:
                    errors.append(f"{pid}#{step['id']}: onFailure recovery path does not exist: {failure}")
                elif step["id"] not in path["appliesToSteps"]:
                    errors.append(
                        f"{pid}#{step['id']}: recovery path {failure} does not list this step in appliesToSteps"
                    )

        for path in process["recoveryPaths"]:
            for step_id in path["appliesToSteps"]:
                if step_id not in step_id_set:
                    errors.append(f"{pid}: recovery path {path['id']} refers to unknown step: {step_id}")
            if target := path.get("targetStepId"):
                if target not in step_id_set:
                    errors.append(f"{pid}: recovery path {path['id']} targets unknown step: {target}")

        optional = {step["id"] for step in steps if step.get("optional")}
        for completion in process["completionConditions"]:
            for step_id in completion.get("requiresSteps", []):
                if step_id not in step_id_set:
                    errors.append(f"{pid}: completion condition {completion['id']} requires unknown step: {step_id}")
                elif step_id in optional:
                    warnings.append(
                        f"{pid}: completion condition {completion['id']} requires optional step {step_id}"
                    )

    return errors, warnings


def recovery_by_id_ids(process: dict[str, Any]) -> list[str]:
    return [path["id"] for path in process["recoveryPaths"]]


# --------------------------------------------------------------------------------------
# Read models and cards
# --------------------------------------------------------------------------------------


def policy_summary(policy: dict[str, Any]) -> dict[str, Any]:
    """The pinned-injection record: everything the assembler needs, nothing it must search for."""
    return {
        "id": policy["id"],
        "title": policy["title"],
        "policyText": policy["policyText"],
        "enforcement": policy["enforcement"],
        "priority": policy["priority"],
        "scope": policy["scope"],
        "activation": policy["activation"],
        "supersedes": policy.get("supersedes", []),
        "status": policy["status"],
        "contentRevision": policy["verification"]["contentRevision"],
        "contentHash": bh.content_hash(policy),
    }


def compile_policy_index(policies: list[dict[str, Any]], processes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0",
        "note": "Pinned policy records. The context assembler reads this directly; mandatory policies are never retrieved by search.",
        "policies": [policy_summary(p) for p in sorted(policies, key=lambda p: (-p["priority"], p["id"]))],
        "processes": [
            {
                "id": process["id"],
                "title": process["title"],
                "contentRevision": process["verification"]["contentRevision"],
                "requiredPolicies": process["requiredPolicies"],
                "stepIds": [step["id"] for step in process["steps"]],
            }
            for process in sorted(processes, key=lambda p: p["id"])
        ],
    }


def compile_process_catalog(processes: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0",
        "processes": sorted(processes, key=lambda p: (p["locale"], p["title"], p["id"])),
    }


def compile_policy_cards(policies: list[dict[str, Any]]) -> list[dict[str, Any]]:
    cards: list[dict[str, Any]] = []
    for policy in policies:
        text = f"{policy['title']}\n{policy['policyText']}"
        if rationale := policy.get("rationale"):
            text += f"\n{rationale}"
        mandatory = policy["enforcement"] == "mandatory"
        cards.append(
            {
                "id": policy["id"],
                "documentId": policy["id"],
                "chunkId": "policy",
                "kind": "policy",
                "locale": policy["locale"],
                "title": policy["title"],
                "text": text,
                "tokens": bh.stable_tokens(text, policy["id"]),
                "entities": [],
                "relations": list(policy.get("supersedes", [])),
                "helpIds": list(policy["scope"].get("helpIds", [])),
                "status": policy["status"],
                "contentHash": bh.content_hash(policy),
                "enforcement": policy["enforcement"],
                "priority": policy["priority"],
                "retrieval": {
                    "mode": "pinned" if mandatory else "searchable",
                    "note": "Injected by the context assembler when applicable; must not depend on search."
                    if mandatory
                    else "Injected when applicable and budget allows; may also be found by search.",
                },
            }
        )
    return cards


def compile_process_cards(processes: list[dict[str, Any]]) -> list[dict[str, Any]]:
    cards: list[dict[str, Any]] = []
    for process in processes:
        pid = process["id"]
        steps = process["steps"]
        step_ids = [step["id"] for step in steps]
        policy_ids = list(process["requiredPolicies"])
        help_ids = list(process["applicability"].get("helpIds", []))
        base = {
            "documentId": pid,
            "locale": process["locale"],
            "entities": [],
            "helpIds": help_ids,
            "status": process["status"],
            "retrieval": {"mode": "searchable"},
        }

        outline = "\n".join(f"{i}. {step['title']}" for i, step in enumerate(steps, start=1))
        summary_text = f"{process['title']}\n{process['purpose']}\n{outline}"
        cards.append(
            {
                **base,
                "id": pid,
                "chunkId": "summary",
                "kind": "process",
                "title": process["title"],
                "text": summary_text,
                "tokens": bh.stable_tokens(
                    summary_text, pid, *process.get("keywords", []), *process.get("alternateQueries", [])
                ),
                "relations": policy_ids,
                "stepIds": step_ids,
                "requiredPolicies": policy_ids,
                "contentHash": bh.content_hash(
                    {"title": process["title"], "purpose": process["purpose"], "stepIds": step_ids}
                ),
            }
        )

        for index, step in enumerate(steps):
            text = f"{step['title']}\n{step['instruction']}\nExpected result: {step['expectedResult']}"
            cards.append(
                {
                    **base,
                    "id": f"{pid}#{step['id']}",
                    "chunkId": step["id"],
                    "kind": "process-step",
                    "title": step["title"],
                    "text": text,
                    "tokens": bh.stable_tokens(text, process["title"], *process.get("keywords", [])),
                    "relations": [pid] + policy_ids + list(step.get("policyIds", [])),
                    "stepIndex": index,
                    "previousStepId": step_ids[index - 1] if index > 0 else None,
                    "nextStepId": step_ids[index + 1] if index + 1 < len(step_ids) else None,
                    "requiredPolicies": sorted(set(policy_ids) | set(step.get("policyIds", []))),
                    "onFailure": step.get("onFailure"),
                    "contentHash": bh.content_hash(step),
                }
            )

        for path in process["recoveryPaths"]:
            text = f"{path['title']}\nWhen: {path['trigger']}\n" + "\n".join(path["actions"])
            cards.append(
                {
                    **base,
                    "id": f"{pid}#recovery-{path['id']}",
                    "chunkId": f"recovery-{path['id']}",
                    "kind": "process-recovery",
                    "title": path["title"],
                    "text": text,
                    "tokens": bh.stable_tokens(text, process["title"]),
                    "relations": [pid] + [f"{pid}#{s}" for s in path["appliesToSteps"]],
                    "appliesToSteps": list(path["appliesToSteps"]),
                    "outcome": path["outcome"],
                    "targetStepId": path.get("targetStepId"),
                    "contentHash": bh.content_hash(path),
                }
            )

        for item in process["cancellation"]:
            text = f"Cancel: {item['condition']}\n{item['effect']}"
            cards.append(
                {
                    **base,
                    "id": f"{pid}#cancel-{item['id']}",
                    "chunkId": f"cancel-{item['id']}",
                    "kind": "process-cancellation",
                    "title": f"Cancelling: {process['title']}",
                    "text": text,
                    "tokens": bh.stable_tokens(text, process["title"]),
                    "relations": [pid],
                    "contentHash": bh.content_hash(item),
                }
            )
    return cards


# --------------------------------------------------------------------------------------
# Active process state validation (runtime data)
# --------------------------------------------------------------------------------------

TERMINAL = ("completed", "failed", "cancelled")


def validate_active_state(state: dict[str, Any], process: dict[str, Any]) -> list[str]:
    """Checks a runtime state document against the definition it points at."""
    errors: list[str] = []
    label = state.get("stateId", "?")
    if state["processId"] != process["id"]:
        return [f"{label}: state is for process {state['processId']}, not {process['id']}"]
    revision = process["verification"]["contentRevision"]
    if state["processRevision"] != revision:
        errors.append(
            f"{label}: state was started on revision {state['processRevision']} but the definition is at "
            f"revision {revision}; the run needs an explicit decision (finish on the old revision or restart)"
        )

    order = [step["id"] for step in process["steps"]]
    position = {step_id: i for i, step_id in enumerate(order)}
    recovery_ids = {path["id"] for path in process["recoveryPaths"]}

    completed_ids = [item["stepId"] for item in state["completedSteps"]]
    for step_id in completed_ids:
        if step_id not in position:
            errors.append(f"{label}: completed step does not exist in the process: {step_id}")
    errors.extend(f"{label}: step completed more than once: {s}" for s in bh.duplicate_values(completed_ids))

    current = state["currentStepId"]
    if state["status"] in TERMINAL:
        if current is not None:
            errors.append(f"{label}: a {state['status']} run must not have a current step")
    else:
        if current not in position:
            errors.append(f"{label}: current step does not exist in the process: {current}")
        elif current in completed_ids:
            errors.append(f"{label}: current step is already completed: {current}")
        else:
            for step_id in completed_ids:
                if step_id in position and position[step_id] > position[current] and state["status"] != "recovering":
                    errors.append(f"{label}: completed step {step_id} comes after the current step {current}")

    if state["status"] == "completed":
        required = _completion_required_steps(process)
        missing = [s for s in required if s not in completed_ids]
        if missing:
            errors.append(f"{label}: run is completed but required steps are not done: {', '.join(missing)}")

    if (path := state.get("activeRecoveryPathId")) and path not in recovery_ids:
        errors.append(f"{label}: active recovery path does not exist in the process: {path}")
    if state["status"] == "recovering" and not state.get("activeRecoveryPathId"):
        errors.append(f"{label}: a recovering run must name its activeRecoveryPathId")

    for item in state["observations"]:
        if (step_id := item.get("stepId")) and step_id not in position:
            errors.append(f"{label}: observation refers to unknown step: {step_id}")
    for item in state["unresolvedQuestions"]:
        if (step_id := item.get("stepId")) and step_id not in position:
            errors.append(f"{label}: question refers to unknown step: {step_id}")
    errors.extend(
        f"{label}: duplicate question id: {q}" for q in bh.duplicate_values(i["id"] for i in state["unresolvedQuestions"])
    )
    if state["updatedAt"] < state["createdAt"]:
        errors.append(f"{label}: updatedAt is earlier than createdAt")
    return errors


def _completion_required_steps(process: dict[str, Any]) -> list[str]:
    required: list[str] = []
    for condition in process["completionConditions"]:
        listed = condition.get("requiresSteps")
        if listed is None:
            listed = [s["id"] for s in process["steps"] if not s.get("optional")]
        for step_id in listed:
            if step_id not in required:
                required.append(step_id)
    return required


# --------------------------------------------------------------------------------------
# Context assembly (reference implementation of the architecture rules)
# --------------------------------------------------------------------------------------


class ContextBudgetError(Exception):
    """The parts of a request that can never be dropped do not fit in the budget."""


class MissingDefinitionError(Exception):
    """A policy or process the request depends on is not available. Assembly must fail loudly, not degrade silently."""


def _request_facts(request: dict[str, Any], state: dict[str, Any] | None) -> dict[str, set[str]]:
    facts = {name: set(values) for name, values in request.get("facts", {}).items()}
    facts["helpId"] = set(request.get("helpIds", []))
    if state and state["status"] not in TERMINAL:
        facts["processId"] = {state["processId"]}
        facts["processStepId"] = {state["currentStepId"]}
    return facts


def _in_scope(scope: dict[str, Any], request: dict[str, Any], state: dict[str, Any] | None) -> bool:
    for key, request_key in (("products", "product"), ("audiences", "audience"), ("platforms", "platform")):
        allowed = scope.get(key)
        if allowed and request.get(request_key) not in allowed:
            return False
    if scope.get("helpIds") and not set(scope["helpIds"]) & set(request.get("helpIds", [])):
        return False
    if scope.get("processIds"):
        active = {state["processId"]} if state and state["status"] not in TERMINAL else set()
        if not set(scope["processIds"]) & active:
            return False
    return True


def _condition_holds(condition: dict[str, Any], facts: dict[str, set[str]]) -> bool:
    hit = bool(set(condition["values"]) & facts.get(condition["fact"], set()))
    return not hit if condition.get("negate") else hit


def _activated(activation: dict[str, Any], facts: dict[str, set[str]]) -> bool:
    if activation["mode"] == "always":
        return True
    results = [_condition_holds(c, facts) for c in activation["conditions"]]
    return all(results) if activation.get("match") == "all" else any(results)


def applicable_policies(
    policies: list[dict[str, Any]],
    request: dict[str, Any],
    state: dict[str, Any] | None = None,
    process: dict[str, Any] | None = None,
) -> list[dict[str, Any]]:
    """Every policy that applies to this request, in injection order.

    Applicability comes from the request's structured facts and the active process only. Nothing here
    consults search results. Policies required by the active process or its current step are forced in as
    mandatory. Superseded policies are removed. Order: mandatory before advisory, then priority, then id.
    """
    by_id = {p["id"]: p for p in policies}
    facts = _request_facts(request, state)
    chosen: dict[str, dict[str, Any]] = {}

    for policy in policies:
        if policy["status"] == "deprecated":
            continue
        if _in_scope(policy["scope"], request, state) and _activated(policy["activation"], facts):
            chosen[policy["id"]] = {**policy, "appliedBecause": "applicable"}

    forced: list[tuple[str, str]] = []
    if state and state["status"] not in TERMINAL and process is not None:
        forced.extend((pid, "required by active process") for pid in process["requiredPolicies"])
        step = next((s for s in process["steps"] if s["id"] == state["currentStepId"]), None)
        if step:
            forced.extend((pid, "required by current step") for pid in step.get("policyIds", []))
    seen_forced: set[str] = set()
    for policy_id, reason in forced:
        if policy_id in seen_forced:  # the broadest reason (process, then step) is the one recorded
            continue
        seen_forced.add(policy_id)
        policy = by_id.get(policy_id)
        if policy is None:
            raise MissingDefinitionError(f"policy required by the active process is not available: {policy_id}")
        chosen[policy_id] = {**policy, "enforcement": "mandatory", "appliedBecause": reason}

    for policy in list(chosen.values()):
        for other in policy.get("supersedes", []):
            if other in chosen and chosen[other]["priority"] <= policy["priority"]:
                del chosen[other]

    return sorted(
        chosen.values(),
        key=lambda p: (0 if p["enforcement"] == "mandatory" else 1, -p["priority"], p["id"]),
    )


def _process_section(process: dict[str, Any], state: dict[str, Any]) -> dict[str, Any]:
    return {
        "definition": {
            "id": process["id"],
            "revision": process["verification"]["contentRevision"],
            "title": process["title"],
            "purpose": process["purpose"],
            "steps": [
                {k: step[k] for k in ("id", "title", "instruction", "expectedResult", "onFailure") if k in step}
                for step in process["steps"]
            ],
            "recoveryPaths": process["recoveryPaths"],
            "completionConditions": process["completionConditions"],
            "cancellation": process["cancellation"],
        },
        "state": {
            key: state[key]
            for key in (
                "stateId",
                "status",
                "currentStepId",
                "activeRecoveryPathId",
                "completedSteps",
                "observations",
                "unresolvedQuestions",
                "createdAt",
                "updatedAt",
            )
            if key in state
        },
    }


def _size(value: Any) -> int:
    return len(bh.json.dumps(value, ensure_ascii=False))


def assemble_context(
    request: dict[str, Any],
    *,
    policies: list[dict[str, Any]],
    processes: list[dict[str, Any]],
    active_state: dict[str, Any] | None,
    retrieved: list[dict[str, Any]],
    conversation: list[dict[str, Any]],
    user_request: str,
    budget: int | None = None,
) -> dict[str, Any]:
    """Rebuilds the whole agent context for one request, from scratch, in the required order.

    1 mandatory policies, 2 active process definition and state, 3 help and product knowledge,
    4 conversation, 5 the current user request.

    Nothing is carried over from a previous turn: policies come from the definitions plus this request's
    facts, the process position comes from the state record, and conversation history is only ever
    section 4. Sections 1, 2 and 5 are never trimmed; if they do not fit the budget assembly fails.
    Advisory policies, knowledge and conversation are then added in that order of priority while they fit
    (conversation newest first). Retrieval results are reference material: they cannot displace,
    reorder or contradict a policy, and policy or process records that come back from search are dropped
    because policies are injected by the assembler, not found by search.
    """
    process = None
    if active_state and active_state["status"] not in TERMINAL:
        process = next((p for p in processes if p["id"] == active_state["processId"]), None)
        if process is None:
            raise MissingDefinitionError(f"active process definition is not available: {active_state['processId']}")
        problems = validate_active_state(active_state, process)
        if problems:
            raise MissingDefinitionError("; ".join(problems))

    applicable = applicable_policies(policies, request, active_state, process)
    mandatory = [p for p in applicable if p["enforcement"] == "mandatory"]
    advisory = [p for p in applicable if p["enforcement"] != "mandatory"]

    def policy_entry(policy: dict[str, Any]) -> dict[str, Any]:
        return {
            "id": policy["id"],
            "title": policy["title"],
            "text": policy["policyText"],
            "priority": policy["priority"],
            "enforcement": policy["enforcement"],
            "appliedBecause": policy["appliedBecause"],
        }

    sections: dict[str, Any] = {
        "policies": {"mandatory": [policy_entry(p) for p in mandatory], "advisory": []},
        "activeProcess": _process_section(process, active_state) if process else None,
        "knowledge": [],
        "conversation": [],
        "userRequest": user_request,
    }

    used = _size(sections["policies"]["mandatory"]) + _size(sections["activeProcess"]) + _size(user_request)
    if budget is not None and used > budget:
        raise ContextBudgetError(
            f"mandatory policies, active process and the user request need {used} characters; budget is {budget}"
        )
    remaining = None if budget is None else budget - used

    def take(entry: Any) -> bool:
        nonlocal remaining
        cost = _size(entry)
        if remaining is not None and cost > remaining:
            return False
        if remaining is not None:
            remaining -= cost
        return True

    for policy in advisory:
        entry = policy_entry(policy)
        if take(entry):
            sections["policies"]["advisory"].append(entry)

    for card in retrieved:
        if card.get("kind") in ("policy",) or str(card.get("kind", "")).startswith("process"):
            continue
        entry = {"id": card["id"], "title": card.get("title", ""), "text": card["text"], "status": card.get("status")}
        if take(entry):
            sections["knowledge"].append(entry)

    kept: list[dict[str, Any]] = []
    for turn in reversed(conversation):
        if take(turn):
            kept.append(turn)
        else:
            break
    sections["conversation"] = list(reversed(kept))

    return {
        "order": ["policies", "activeProcess", "knowledge", "conversation", "userRequest"],
        "precedence": "Mandatory policies outrank everything below them. Knowledge and conversation never override a policy.",
        "sections": sections,
    }


def render_context(assembled: dict[str, Any]) -> str:
    """Plain-text rendering of an assembled context, in section order, for models that take text."""
    s = assembled["sections"]
    out: list[str] = ["## Policies you must follow (highest authority; nothing below can override these)"]
    for p in s["policies"]["mandatory"]:
        out.append(f"- [{p['id']}] {p['title']}: {p['text']}")
    if s["policies"]["advisory"]:
        out.append("\n## Guidance (follow unless a policy above says otherwise)")
        for p in s["policies"]["advisory"]:
            out.append(f"- [{p['id']}] {p['title']}: {p['text']}")
    if s["activeProcess"]:
        d, st = s["activeProcess"]["definition"], s["activeProcess"]["state"]
        out.append(f"\n## Active process: {d['title']} (revision {d['revision']})")
        out.append(f"Status: {st['status']}. Current step: {st['currentStepId']}.")
        done = {c["stepId"] for c in st["completedSteps"]}
        for step in d["steps"]:
            mark = "done" if step["id"] in done else ("CURRENT" if step["id"] == st["currentStepId"] else "todo")
            out.append(f"- ({mark}) {step['id']}: {step['instruction']} => {step['expectedResult']}")
        for item in st["observations"]:
            out.append(f"Observed: {item['text']}")
        for item in st["unresolvedQuestions"]:
            out.append(f"Unresolved question: {item['question']}")
    if s["knowledge"]:
        out.append("\n## Product knowledge (reference material; if it conflicts with a policy, the policy wins)")
        for k in s["knowledge"]:
            out.append(f"[{k['id']}] {k['text']}")
    if s["conversation"]:
        out.append("\n## Conversation so far")
        for turn in s["conversation"]:
            out.append(f"{turn['role']}: {turn['text']}")
    out.append("\n## Current user request")
    out.append(s["userRequest"])
    return "\n".join(out)
