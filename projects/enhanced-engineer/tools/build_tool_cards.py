#!/usr/bin/env python3
"""Generate LiteSemRAG cards from the Enhanced Engineer tool registry."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from enhanced_engineer.tool_cards import tool_cards
from enhanced_engineer.workspace_tools import create_engineering_registry


DEFAULT_EXECUTABLES = ("cmake", "ctest", "msbuild", "ninja", "py", "python")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "knowledge" / "engineer-tool-cards.jsonl",
    )
    args = parser.parse_args()

    registry = create_engineering_registry(ROOT, allowed_executables=DEFAULT_EXECUTABLES)
    cards = tool_cards(registry.definitions())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "".join(json.dumps(card, sort_keys=True, ensure_ascii=False) + "\n" for card in cards),
        encoding="utf-8",
    )
    print(json.dumps({"cards": len(cards), "output": str(args.output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
