"""Deterministic SQLite LiteSemRAG index for authored semantic cards.

The index has explicit document, chunk, semantic and token graph layers. It does
not ask an LLM to index, summarize or rewrite source material. Authored cards
remain authoritative; this database is a disposable read model.
"""

from __future__ import annotations

import json
import re
import sqlite3
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


TOKEN_RE = re.compile(r"[a-z0-9]+(?:[._-][a-z0-9]+)*", re.IGNORECASE)
STOP_WORDS = {
    "a", "an", "and", "are", "as", "at", "be", "by", "can", "do", "for",
    "from", "how", "i", "in", "is", "it", "of", "on", "or", "the", "this",
    "to", "what", "when", "where", "with", "you",
}


@dataclass(frozen=True)
class RetrievalHit:
    card_id: str
    document_id: str
    chunk_id: str
    kind: str
    title: str
    text: str
    score: float
    reasons: tuple[str, ...]
    source: str | None
    metadata: dict[str, Any]


def query_tokens(text: str) -> list[str]:
    values: list[str] = []
    for match in TOKEN_RE.finditer(text.lower()):
        token = match.group(0)
        if token in STOP_WORDS or len(token) < 2 or token in values:
            continue
        values.append(token)
    return values


def kind_query_factor(kind: str, tokens: Sequence[str]) -> float:
    token_set = set(tokens)
    if kind in {"process-recovery", "troubleshooting"}:
        failure_terms = {"cannot", "error", "fail", "failed", "failure", "problem", "recover", "restore", "wrong"}
        return 1.0 if token_set.intersection(failure_terms) else 0.6
    if kind == "process-cancellation":
        cancellation_terms = {"abort", "cancel", "quit", "stop", "undo"}
        return 1.0 if token_set.intersection(cancellation_terms) else 0.55
    return 1.0


class LiteSemRAG:
    """Build and query a compact semantic-card graph."""

    def __init__(self, database: str | Path = ":memory:") -> None:
        self.database = str(database)
        self.connection = sqlite3.connect(self.database)
        self.connection.row_factory = sqlite3.Row
        self.connection.execute("PRAGMA foreign_keys = ON")
        self._create_schema()

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> "LiteSemRAG":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def _create_schema(self) -> None:
        self.connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS graph_nodes (
                id TEXT PRIMARY KEY,
                layer TEXT NOT NULL CHECK(layer IN
                    ('document', 'chunk', 'semantic', 'token', 'entity', 'context')),
                kind TEXT NOT NULL,
                label TEXT NOT NULL,
                metadata_json TEXT NOT NULL DEFAULT '{}'
            );

            CREATE TABLE IF NOT EXISTS graph_edges (
                source_id TEXT NOT NULL,
                target_id TEXT NOT NULL,
                relation TEXT NOT NULL,
                weight REAL NOT NULL DEFAULT 1.0,
                PRIMARY KEY (source_id, target_id, relation),
                FOREIGN KEY (source_id) REFERENCES graph_nodes(id) ON DELETE CASCADE,
                FOREIGN KEY (target_id) REFERENCES graph_nodes(id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS cards (
                id TEXT PRIMARY KEY,
                document_id TEXT NOT NULL,
                chunk_id TEXT NOT NULL,
                semantic_node_id TEXT NOT NULL UNIQUE,
                kind TEXT NOT NULL,
                locale TEXT NOT NULL,
                title TEXT NOT NULL,
                text TEXT NOT NULL,
                status TEXT NOT NULL,
                content_hash TEXT NOT NULL,
                source TEXT,
                metadata_json TEXT NOT NULL,
                FOREIGN KEY (semantic_node_id) REFERENCES graph_nodes(id) ON DELETE CASCADE
            );

            CREATE TABLE IF NOT EXISTS token_occurrences (
                token TEXT NOT NULL,
                semantic_node_id TEXT NOT NULL,
                field TEXT NOT NULL,
                weight REAL NOT NULL,
                PRIMARY KEY (token, semantic_node_id, field),
                FOREIGN KEY (semantic_node_id) REFERENCES graph_nodes(id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_edges_target ON graph_edges(target_id, relation);
            CREATE INDEX IF NOT EXISTS idx_occurrences_token ON token_occurrences(token);
            CREATE INDEX IF NOT EXISTS idx_cards_document ON cards(document_id);

            CREATE VIRTUAL TABLE IF NOT EXISTS card_fts USING fts5(
                card_id UNINDEXED,
                title,
                text,
                tokens,
                tokenize = 'unicode61 remove_diacritics 2'
            );
            """
        )
        self.connection.commit()

    def clear(self) -> None:
        self.connection.executescript(
            """
            DELETE FROM card_fts;
            DELETE FROM token_occurrences;
            DELETE FROM cards;
            DELETE FROM graph_edges;
            DELETE FROM graph_nodes;
            """
        )
        self.connection.commit()

    def ingest_jsonl(self, path: str | Path, *, replace: bool = True) -> int:
        cards: list[dict[str, Any]] = []
        with Path(path).open("r", encoding="utf-8") as handle:
            for line_number, line in enumerate(handle, 1):
                if not line.strip():
                    continue
                try:
                    cards.append(json.loads(line))
                except json.JSONDecodeError as error:
                    raise ValueError(f"{path}:{line_number}: {error}") from error
        return self.ingest_cards(cards, replace=replace)

    def ingest_cards(self, cards: Iterable[dict[str, Any]], *, replace: bool = True) -> int:
        values = list(cards)
        if replace:
            self.clear()

        cursor = self.connection.cursor()
        for card in values:
            self._ingest_card(cursor, card)
        self._rebuild_cooccurrence_edges(cursor)
        self.connection.commit()
        return len(values)

    def _rebuild_cooccurrence_edges(self, cursor: sqlite3.Cursor) -> None:
        """Connect semantic meanings that share multiple useful lexical anchors."""
        cursor.execute("DELETE FROM graph_edges WHERE relation = 'CO_OCCURS'")
        pairs = cursor.execute(
            """
            SELECT
                a.semantic_node_id AS source_id,
                b.semantic_node_id AS target_id,
                count(*) AS shared_count,
                sum(min(a.weight, b.weight)) AS shared_weight
            FROM token_occurrences AS a
            JOIN token_occurrences AS b
              ON a.token = b.token
             AND a.semantic_node_id < b.semantic_node_id
            GROUP BY a.semantic_node_id, b.semantic_node_id
            HAVING shared_count >= 2
            """
        ).fetchall()
        for pair in pairs:
            weight = min(3.0, 0.2 * pair["shared_weight"])
            self._edge(cursor, pair["source_id"], pair["target_id"], "CO_OCCURS", weight)
            self._edge(cursor, pair["target_id"], pair["source_id"], "CO_OCCURS", weight)

    def _node(
        self,
        cursor: sqlite3.Cursor,
        node_id: str,
        layer: str,
        kind: str,
        label: str,
        metadata: dict[str, Any] | None = None,
    ) -> None:
        cursor.execute(
            """
            INSERT INTO graph_nodes(id, layer, kind, label, metadata_json)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                layer=excluded.layer,
                kind=excluded.kind,
                label=excluded.label,
                metadata_json=excluded.metadata_json
            """,
            (node_id, layer, kind, label, json.dumps(metadata or {}, sort_keys=True)),
        )

    @staticmethod
    def _edge(
        cursor: sqlite3.Cursor,
        source: str,
        target: str,
        relation: str,
        weight: float = 1.0,
    ) -> None:
        cursor.execute(
            """
            INSERT INTO graph_edges(source_id, target_id, relation, weight)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(source_id, target_id, relation)
            DO UPDATE SET weight=excluded.weight
            """,
            (source, target, relation, weight),
        )

    def _ingest_card(self, cursor: sqlite3.Cursor, card: dict[str, Any]) -> None:
        required = (
            "id", "documentId", "chunkId", "kind", "locale", "title", "text",
            "status", "contentHash",
        )
        missing = [key for key in required if key not in card]
        if missing:
            raise ValueError(f"card is missing required fields {missing}: {card.get('id', '<unknown>')}")

        card_id = str(card["id"])
        document_id = str(card["documentId"])
        chunk_id = f"chunk:{card_id}"
        semantic_id = f"semantic:{card_id}"
        document_node = f"document:{document_id}"

        self._node(cursor, document_node, "document", "source", document_id)
        self._node(cursor, chunk_id, "chunk", card["kind"], card["title"])
        self._node(
            cursor,
            semantic_id,
            "semantic",
            card["kind"],
            card["title"],
            {"status": card["status"], "contentHash": card["contentHash"]},
        )
        self._edge(cursor, document_node, chunk_id, "CONTAINS", 1.0)
        self._edge(cursor, chunk_id, semantic_id, "EXPRESSES", 1.0)

        source = card.get("source")
        metadata = {
            key: value
            for key, value in card.items()
            if key not in {"text", "tokens"}
        }
        cursor.execute(
            """
            INSERT INTO cards(
                id, document_id, chunk_id, semantic_node_id, kind, locale,
                title, text, status, content_hash, source, metadata_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                document_id=excluded.document_id,
                chunk_id=excluded.chunk_id,
                semantic_node_id=excluded.semantic_node_id,
                kind=excluded.kind,
                locale=excluded.locale,
                title=excluded.title,
                text=excluded.text,
                status=excluded.status,
                content_hash=excluded.content_hash,
                source=excluded.source,
                metadata_json=excluded.metadata_json
            """,
            (
                card_id, document_id, str(card["chunkId"]), semantic_id, card["kind"],
                card["locale"], card["title"], card["text"], card["status"],
                card["contentHash"], source, json.dumps(metadata, sort_keys=True),
            ),
        )
        cursor.execute("DELETE FROM card_fts WHERE card_id = ?", (card_id,))
        cursor.execute("DELETE FROM token_occurrences WHERE semantic_node_id = ?", (semantic_id,))

        authored_tokens = [str(token).lower() for token in card.get("tokens", [])]
        title_tokens = query_tokens(card["title"])
        text_tokens = query_tokens(card["text"])
        token_fields: dict[str, tuple[str, float]] = {}
        for token in text_tokens:
            token_fields.setdefault(token, ("text", 1.0))
        for token in authored_tokens:
            token_fields[token] = ("authored", 2.5)
        for token in title_tokens:
            token_fields[token] = ("title", 4.0)

        for token, (field, weight) in token_fields.items():
            token_id = f"token:{token}"
            self._node(cursor, token_id, "token", "lexeme", token)
            self._edge(cursor, token_id, semantic_id, "LEXICALIZES", weight)
            cursor.execute(
                "INSERT INTO token_occurrences(token, semantic_node_id, field, weight) VALUES (?, ?, ?, ?)",
                (token, semantic_id, field, weight),
            )

        cursor.execute(
            "INSERT INTO card_fts(card_id, title, text, tokens) VALUES (?, ?, ?, ?)",
            (card_id, card["title"], card["text"], " ".join(sorted(token_fields))),
        )

        for target in card.get("relations", []):
            target_semantic = f"semantic:{target}"
            self._node(cursor, target_semantic, "semantic", "reference", str(target))
            self._edge(cursor, semantic_id, target_semantic, "RELATED_TO", 1.5)
        for policy_id in card.get("requiredPolicies", []):
            target_semantic = f"semantic:{policy_id}"
            self._node(cursor, target_semantic, "semantic", "policy", str(policy_id))
            self._edge(cursor, semantic_id, target_semantic, "REQUIRES_POLICY", 3.0)
        for entity in card.get("entities", []):
            entity_id = f"entity:{entity}"
            self._node(cursor, entity_id, "entity", "entity", str(entity))
            self._edge(cursor, semantic_id, entity_id, "MENTIONS", 1.0)
        for help_id in card.get("helpIds", []):
            context_id = f"context:{help_id}"
            self._node(cursor, context_id, "context", "help-id", str(help_id))
            self._edge(cursor, context_id, semantic_id, "RESOLVES_TO", 4.0)

    def statistics(self) -> dict[str, Any]:
        layers = {
            row["layer"]: row["count"]
            for row in self.connection.execute(
                "SELECT layer, count(*) AS count FROM graph_nodes GROUP BY layer ORDER BY layer"
            )
        }
        return {
            "cards": self.connection.execute("SELECT count(*) FROM cards").fetchone()[0],
            "edges": self.connection.execute("SELECT count(*) FROM graph_edges").fetchone()[0],
            "layers": layers,
        }

    def retrieve(
        self,
        query: str,
        *,
        top_k: int = 8,
        eligible_statuses: Sequence[str] = ("verified", "draft", "proposed"),
    ) -> list[RetrievalHit]:
        tokens = query_tokens(query)
        if not tokens:
            return []

        scores: dict[str, float] = {}
        reasons: dict[str, set[str]] = {}

        placeholders = ",".join("?" for _ in eligible_statuses)
        fts_expression = " OR ".join(f'"{token.replace(chr(34), chr(34) * 2)}"' for token in tokens)
        fts_rows = self.connection.execute(
            f"""
            SELECT f.card_id, bm25(card_fts, 6.0, 1.0, 2.0) AS rank
            FROM card_fts AS f
            JOIN cards AS c ON c.id = f.card_id
            WHERE card_fts MATCH ? AND c.status IN ({placeholders})
            ORDER BY rank
            LIMIT 40
            """,
            (fts_expression, *eligible_statuses),
        ).fetchall()
        for position, row in enumerate(fts_rows):
            scores[row["card_id"]] = scores.get(row["card_id"], 0.0) + 8.0 / (1.0 + position)
            reasons.setdefault(row["card_id"], set()).add("full-text match")

        for token in tokens:
            rows = self.connection.execute(
                f"""
                SELECT c.id AS card_id, o.field, o.weight
                FROM token_occurrences AS o
                JOIN cards AS c ON c.semantic_node_id = o.semantic_node_id
                WHERE o.token = ? AND c.status IN ({placeholders})
                """,
                (token, *eligible_statuses),
            ).fetchall()
            for row in rows:
                scores[row["card_id"]] = scores.get(row["card_id"], 0.0) + row["weight"]
                reasons.setdefault(row["card_id"], set()).add(
                    f"{row['field']} token '{token}'"
                )

        initial_ids = sorted(scores, key=scores.get, reverse=True)[:12]
        graph_bonus: dict[str, float] = {}
        graph_reason: dict[str, str] = {}
        for card_id in initial_ids:
            semantic_id = f"semantic:{card_id}"
            related = self.connection.execute(
                """
                SELECT c.id AS card_id, e.relation, e.weight
                FROM graph_edges AS e
                JOIN cards AS c ON c.semantic_node_id = e.target_id
                WHERE e.source_id = ? AND e.relation IN
                    ('RELATED_TO', 'REQUIRES_POLICY', 'CO_OCCURS')
                UNION ALL
                SELECT c.id AS card_id, e.relation, e.weight
                FROM graph_edges AS e
                JOIN cards AS c ON c.semantic_node_id = e.source_id
                WHERE e.target_id = ? AND e.relation IN
                    ('RELATED_TO', 'REQUIRES_POLICY', 'CO_OCCURS')
                """,
                (semantic_id, semantic_id),
            ).fetchall()
            for row in related:
                if row["card_id"] == card_id:
                    continue
                bonus = min(2.0, scores[card_id] * 0.12 * row["weight"])
                if bonus > graph_bonus.get(row["card_id"], 0.0):
                    graph_bonus[row["card_id"]] = bonus
                    graph_reason[row["card_id"]] = (
                        f"graph edge {row['relation']} from {card_id}"
                    )

        for card_id, bonus in graph_bonus.items():
            scores[card_id] = scores.get(card_id, 0.0) + bonus
            reasons.setdefault(card_id, set()).add(graph_reason[card_id])

        ranked = sorted(scores.items(), key=lambda item: (-item[1], item[0]))[:top_k]
        hits: list[RetrievalHit] = []
        for card_id, score in ranked:
            row = self.connection.execute("SELECT * FROM cards WHERE id = ?", (card_id,)).fetchone()
            if row is None:
                continue
            status_factor = {"verified": 1.15, "draft": 1.0, "proposed": 0.85}.get(row["status"], 0.5)
            semantic_factor = kind_query_factor(row["kind"], tokens)
            hits.append(
                RetrievalHit(
                    card_id=row["id"],
                    document_id=row["document_id"],
                    chunk_id=row["chunk_id"],
                    kind=row["kind"],
                    title=row["title"],
                    text=row["text"],
                    score=round(score * status_factor * semantic_factor, 4),
                    reasons=tuple(sorted(reasons.get(card_id, {"graph recovery"}))),
                    source=row["source"],
                    metadata=json.loads(row["metadata_json"]),
                )
            )
        return sorted(hits, key=lambda hit: (-hit.score, hit.card_id))
