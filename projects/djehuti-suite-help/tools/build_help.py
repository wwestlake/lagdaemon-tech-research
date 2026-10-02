#!/usr/bin/env python3
"""Validate DjehutiSuite help, policy and process sources and compile browser/RAG read models."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Iterable

from jsonschema import Draft202012Validator, FormatChecker


TOKEN_RE = re.compile(r"[a-z0-9]+(?:[._-][a-z0-9]+)*", re.IGNORECASE)


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def schema_errors(instance: Any, schema: dict[str, Any], label: str) -> list[str]:
    validator = Draft202012Validator(schema, format_checker=FormatChecker())
    errors: list[str] = []
    for error in sorted(validator.iter_errors(instance), key=lambda item: list(item.path)):
        location = ".".join(str(part) for part in error.absolute_path) or "$"
        errors.append(f"{label}:{location}: {error.message}")
    return errors


def duplicate_values(values: Iterable[str]) -> list[str]:
    counts = Counter(values)
    return sorted(value for value, count in counts.items() if count > 1)


def block_text(block: dict[str, Any]) -> str:
    parts: list[str] = []
    for key in ("title", "text", "intro", "term", "definition", "cause", "resolution"):
        if value := block.get(key):
            parts.append(value)
    parts.extend(block.get("symptoms", []))
    for step in block.get("steps", []):
        parts.append(step["instruction"])
        if result := step.get("expectedResult"):
            parts.append(result)
    for chapter in block.get("chapters", []):
        parts.append(chapter["title"])
    return "\n".join(parts)


def stable_tokens(*values: str) -> list[str]:
    return sorted({match.group(0).lower() for value in values for match in TOKEN_RE.finditer(value)})


def content_hash(value: Any) -> str:
    payload = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return "sha256:" + hashlib.sha256(payload.encode("utf-8")).hexdigest()


def validate_links(topics: list[dict[str, Any]], inventory: dict[str, Any]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    topic_by_id = {topic["id"]: topic for topic in topics}
    inventory_by_id = {item["helpId"]: item for item in inventory["items"]}

    duplicates = duplicate_values(topic["id"] for topic in topics)
    errors.extend(f"duplicate topic id: {item}" for item in duplicates)
    duplicates = duplicate_values(item["helpId"] for item in inventory["items"])
    errors.extend(f"duplicate inventory helpId: {item}" for item in duplicates)

    covered: set[str] = set()
    for topic in topics:
        block_ids = [block["id"] for block in topic["blocks"]]
        block_id_set = set(block_ids)
        errors.extend(
            f"{topic['id']}: duplicate block id: {item}"
            for item in duplicate_values(block_ids)
        )

        step_ids = [
            step["id"]
            for block in topic["blocks"]
            for step in block.get("steps", [])
        ]
        errors.extend(
            f"{topic['id']}: duplicate step id: {item}"
            for item in duplicate_values(step_ids)
        )

        for context in topic["contexts"]:
            help_id = context["helpId"]
            item = inventory_by_id.get(help_id)
            if item is None:
                errors.append(f"{topic['id']}: context helpId not in inventory: {help_id}")
                continue
            covered.add(help_id)
            if context["verifiedBehaviorSignature"] != item["behaviorSignature"]:
                errors.append(
                    f"{topic['id']}: stale behavior signature for {help_id}; "
                    f"reviewed {context['verifiedBehaviorSignature']}, "
                    f"inventory has {item['behaviorSignature']}"
                )
            if anchor := context.get("anchorBlockId"):
                if anchor not in block_id_set:
                    errors.append(f"{topic['id']}: context anchor does not exist: {anchor}")

        for relation in topic["relations"]:
            target = topic_by_id.get(relation["topicId"])
            if target is None:
                errors.append(
                    f"{topic['id']}: relation target does not exist: {relation['topicId']}"
                )
                continue
            if anchor := relation.get("anchorBlockId"):
                if anchor not in {block["id"] for block in target["blocks"]}:
                    errors.append(
                        f"{topic['id']}: relation anchor does not exist: "
                        f"{relation['topicId']}#{anchor}"
                    )

        for block in topic["blocks"]:
            for chapter in block.get("chapters", []):
                for related_id in chapter.get("relatedBlockIds", []):
                    if related_id not in block_id_set:
                        errors.append(
                            f"{topic['id']}#{block['id']}: chapter refers to missing block: {related_id}"
                        )

    for item in inventory["items"]:
        if item["documentation"] == "required" and item["helpId"] not in covered:
            errors.append(f"required helpId is not covered: {item['helpId']}")
        elif item["documentation"] == "recommended" and item["helpId"] not in covered:
            warnings.append(f"recommended helpId is not covered: {item['helpId']}")

    return errors, warnings


def compile_catalog(topics: list[dict[str, Any]], inventory: dict[str, Any]) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0",
        "product": inventory["product"],
        "buildVersion": inventory["buildVersion"],
        "sourceRevision": inventory["sourceRevision"],
        "topics": sorted(topics, key=lambda topic: (topic["locale"], topic["title"], topic["id"])),
        "searchDocuments": [
            {
                "id": topic["id"],
                "locale": topic["locale"],
                "type": topic["type"],
                "title": topic["title"],
                "summary": topic["summary"],
                "keywords": topic["keywords"],
                "alternateQueries": topic.get("alternateQueries", []),
                "text": "\n".join(block_text(block) for block in topic["blocks"]),
            }
            for topic in topics
        ],
    }


def compile_context_map(topics: list[dict[str, Any]]) -> dict[str, Any]:
    contexts: dict[str, dict[str, str]] = {}
    for topic in topics:
        for context in topic["contexts"]:
            target = {"topicId": topic["id"]}
            if anchor := context.get("anchorBlockId"):
                target["anchorBlockId"] = anchor
            contexts[context["helpId"]] = target
    return {"schemaVersion": "1.0", "contexts": dict(sorted(contexts.items()))}


def compile_cards(topics: list[dict[str, Any]]) -> list[dict[str, Any]]:
    cards: list[dict[str, Any]] = []
    for topic in topics:
        help_ids = [context["helpId"] for context in topic["contexts"]]
        relation_ids = [relation["topicId"] for relation in topic["relations"]]
        topic_text = f"{topic['title']}\n{topic['summary']}"
        cards.append(
            {
                "id": topic["id"],
                "documentId": topic["id"],
                "chunkId": "summary",
                "kind": topic["type"],
                "locale": topic["locale"],
                "title": topic["title"],
                "text": topic_text,
                "tokens": stable_tokens(topic_text, *topic["keywords"], *topic.get("alternateQueries", [])),
                "entities": topic.get("entities", []),
                "relations": relation_ids,
                "helpIds": help_ids,
                "status": topic["status"],
                "contentHash": content_hash({"summary": topic["summary"], "keywords": topic["keywords"]}),
            }
        )
        for block in topic["blocks"]:
            text = block_text(block)
            cards.append(
                {
                    "id": f"{topic['id']}#{block['id']}",
                    "documentId": topic["id"],
                    "chunkId": block["id"],
                    "kind": block["kind"],
                    "locale": topic["locale"],
                    "title": block.get("title", topic["title"]),
                    "text": text,
                    "tokens": stable_tokens(text, topic["title"], *topic["keywords"]),
                    "entities": topic.get("entities", []),
                    "relations": relation_ids,
                    "helpIds": help_ids,
                    "status": topic["status"],
                    "contentHash": content_hash(block),
                    "media": {
                        key: block[key]
                        for key in ("pageUrl", "captionsUrl", "transcriptUrl", "chapters")
                        if key in block
                    },
                }
            )
    return cards


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def load_definitions(directory: Path | None, schema: dict[str, Any] | None, errors: list[str]) -> list[dict[str, Any]]:
    """Loads authored policy or process JSON from a directory, refusing runtime records."""
    if directory is None or schema is None:
        return []
    import context_system

    if not directory.is_dir():
        errors.append(f"definition directory does not exist: {directory}")
        return []
    documents: list[tuple[str, Any]] = []
    for path in sorted(directory.rglob("*.json")):
        documents.append((str(path), load_json(path)))
    errors.extend(context_system.runtime_data_errors(documents))
    loaded: list[dict[str, Any]] = []
    for label, document in documents:
        if isinstance(document, dict) and (
            document.get("runtimeData") is True or document.get("recordKind") in context_system.RUNTIME_MARKERS
        ):
            continue
        schema_problems = schema_errors(document, schema, label)
        errors.extend(schema_problems)
        if not schema_problems:
            loaded.append(document)
    return loaded


def build(args: argparse.Namespace) -> int:
    topic_schema = load_json(args.topic_schema)
    inventory_schema = load_json(args.inventory_schema)
    inventory = load_json(args.inventory)
    topic_paths = sorted(args.topics.rglob("*.json"))
    topics: list[dict[str, Any]] = []
    errors = schema_errors(inventory, inventory_schema, str(args.inventory))

    for path in topic_paths:
        topic = load_json(path)
        errors.extend(schema_errors(topic, topic_schema, str(path)))
        topics.append(topic)

    if not topic_paths:
        errors.append(f"no topic JSON files found under {args.topics}")

    # Policies and processes are optional additions to the help sources.
    policies_dir = getattr(args, "policies", None)
    processes_dir = getattr(args, "processes", None)
    policy_schema = load_json(args.policy_schema) if getattr(args, "policy_schema", None) else None
    process_schema = load_json(args.process_schema) if getattr(args, "process_schema", None) else None
    policies = load_definitions(policies_dir, policy_schema, errors)
    processes = load_definitions(processes_dir, process_schema, errors)

    warnings: list[str] = []
    if not errors:
        link_errors, warnings = validate_links(topics, inventory)
        errors.extend(link_errors)
        if policies or processes:
            import context_system

            definition_errors, definition_warnings = context_system.validate_definitions(policies, processes, inventory)
            errors.extend(definition_errors)
            warnings.extend(definition_warnings)

    report = {
        "schemaVersion": "1.0",
        "ok": not errors,
        "topicCount": len(topics),
        "policyCount": len(policies),
        "processCount": len(processes),
        "inventoryItemCount": len(inventory.get("items", [])),
        "errorCount": len(errors),
        "warningCount": len(warnings),
        "errors": errors,
        "warnings": warnings,
    }

    args.output.mkdir(parents=True, exist_ok=True)
    write_json(args.output / "sync-report.json", report)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    write_json(args.output / "help-catalog.json", compile_catalog(topics, inventory))
    write_json(args.output / "context-map.json", compile_context_map(topics))
    cards = compile_cards(topics)
    if policies or processes:
        import context_system

        write_json(args.output / "policy-index.json", context_system.compile_policy_index(policies, processes))
        write_json(args.output / "process-catalog.json", context_system.compile_process_catalog(processes))
        cards.extend(context_system.compile_policy_cards(policies))
        cards.extend(context_system.compile_process_cards(processes))
    with (args.output / "semantic-cards.jsonl").open("w", encoding="utf-8") as handle:
        for card in cards:
            handle.write(json.dumps(card, ensure_ascii=False, sort_keys=True) + "\n")

    print(
        f"Compiled {len(topics)} topic(s), {len(policies)} policy(ies), {len(processes)} process(es), "
        f"{len(cards)} semantic card(s), and {len(inventory['items'])} inventory item(s)."
    )
    for warning in warnings:
        print(f"WARNING: {warning}", file=sys.stderr)
    return 0


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--topics", type=Path, default=root / "examples" / "topics")
    parser.add_argument("--inventory", type=Path, default=root / "examples" / "help-inventory.json")
    parser.add_argument("--topic-schema", type=Path, default=root / "schemas" / "help-topic.schema.json")
    parser.add_argument(
        "--inventory-schema", type=Path, default=root / "schemas" / "help-inventory.schema.json"
    )
    parser.add_argument("--policies", type=Path, default=root / "examples" / "policies")
    parser.add_argument("--processes", type=Path, default=root / "examples" / "processes")
    parser.add_argument("--policy-schema", type=Path, default=root / "schemas" / "policy.schema.json")
    parser.add_argument("--process-schema", type=Path, default=root / "schemas" / "process.schema.json")
    parser.add_argument("--output", type=Path, default=root / "build")
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(build(parse_args()))
