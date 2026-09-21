"""Generate LiteSemRAG semantic cards directly from registered tools."""

from __future__ import annotations

import hashlib
import json
from typing import Any, Iterable

from .tools import ToolDefinition


def _content_hash(card: dict[str, Any]) -> str:
    canonical = json.dumps(card, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return "sha256:" + hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def tool_card(definition: ToolDefinition) -> dict[str, Any]:
    stable_name = definition.name.replace("_", ".")
    document_id = f"enhanced-engineer.tool.{stable_name}"
    text = (
        f"Tool: {definition.name}\n"
        f"Purpose: {definition.description}\n"
        f"When to use: {definition.usage}\n"
        f"Minimum access: {definition.access.minimum_level.name}.\n"
        f"Risk class: {definition.access.risk.value}.\n"
        f"Workspace scoped: {'yes' if definition.access.workspace_scoped else 'no'}.\n"
        f"Always requires approval: {'yes' if definition.access.always_requires_approval else 'no'}.\n"
        f"Arguments schema: {json.dumps(definition.parameters, sort_keys=True)}"
    )
    card: dict[str, Any] = {
        "id": f"{document_id}#usage",
        "documentId": document_id,
        "chunkId": "usage",
        "kind": "tool",
        "locale": "en-US",
        "title": f"Use {definition.name}",
        "text": text,
        "tokens": sorted(set(definition.tokens) | {definition.name, "tool"}),
        "entities": ["Enhanced Engineer", definition.name],
        "relations": [],
        "helpIds": [document_id],
        "status": "verified",
        "source": "enhanced_engineer tool registry",
    }
    card["contentHash"] = _content_hash(card)
    return card


def tool_cards(definitions: Iterable[ToolDefinition]) -> list[dict[str, Any]]:
    return [tool_card(definition) for definition in sorted(definitions, key=lambda item: item.name)]
