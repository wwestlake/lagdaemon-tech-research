#!/usr/bin/env python3
"""Build a disposable Enhanced Engineer LiteSemRAG database from semantic cards."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from enhanced_engineer.litesemrag import LiteSemRAG


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cards", type=Path, help="Compiled semantic-cards.jsonl")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "enhanced-engineer.db")
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with LiteSemRAG(args.output) as index:
        count = index.ingest_jsonl(args.cards)
        stats = index.statistics()

    print(json.dumps({"ingested": count, "database": str(args.output), **stats}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
