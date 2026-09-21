"""Workspace and bounded process tools for the Enhanced Engineer."""

from __future__ import annotations

import fnmatch
import subprocess
from pathlib import Path
from typing import Any, Mapping, Sequence

from .access import AccessLevel, RiskClass, ToolAccess
from .tools import ToolDefinition, ToolOutput, ToolRegistry


IGNORED_DIRECTORIES = {".git", ".svn", "__pycache__", "build", "node_modules"}


def _object_schema(properties: dict[str, Any], required: Sequence[str] = ()) -> dict[str, Any]:
    return {
        "type": "object",
        "additionalProperties": False,
        "properties": properties,
        "required": list(required),
    }


class WorkspaceToolset:
    def __init__(self, root: str | Path) -> None:
        self.root = Path(root).resolve(strict=False)

    def resolve(self, value: Any) -> Path:
        if not isinstance(value, str) or not value.strip():
            raise ValueError("path must be a non-empty string")
        supplied = Path(value)
        return supplied.resolve(strict=False) if supplied.is_absolute() else (self.root / supplied).resolve(strict=False)

    def display_path(self, path: Path) -> str:
        try:
            return path.relative_to(self.root).as_posix()
        except ValueError:
            return str(path)

    def register(self, registry: ToolRegistry) -> None:
        path = {"type": "string", "description": "Path relative to the open project."}
        registry.register(
            ToolDefinition(
                "workspace_list",
                "List files and directories in the open project.",
                _object_schema(
                    {
                        "path": path,
                        "recursive": {"type": "boolean", "default": False},
                        "limit": {"type": "integer", "minimum": 1, "maximum": 1000, "default": 200},
                    }
                ),
                ToolAccess("workspace_list", AccessLevel.OBSERVE, RiskClass.READ, True),
                "Use before assuming a project layout or choosing a file to inspect.",
                ("files", "folders", "tree", "project", "list"),
                "path",
            ),
            self._list,
            target_resolver=lambda args: self.resolve(args.get("path", ".")),
        )
        registry.register(
            ToolDefinition(
                "workspace_read",
                "Read a UTF-8 project file with line numbers and optional line bounds.",
                _object_schema(
                    {
                        "path": path,
                        "start_line": {"type": "integer", "minimum": 1, "default": 1},
                        "end_line": {"type": "integer", "minimum": 1},
                    },
                    ("path",),
                ),
                ToolAccess("workspace_read", AccessLevel.OBSERVE, RiskClass.READ, True),
                "Read specifications and surrounding source before proposing or making an edit. Use line bounds for large files.",
                ("read", "source", "specification", "documentation", "lines"),
                "path",
            ),
            self._read,
            target_resolver=lambda args: self.resolve(args["path"]),
        )
        registry.register(
            ToolDefinition(
                "workspace_search",
                "Search text files in the project and return matching paths, lines, and excerpts.",
                _object_schema(
                    {
                        "query": {"type": "string", "minLength": 1},
                        "path": path,
                        "file_pattern": {"type": "string", "default": "*"},
                        "limit": {"type": "integer", "minimum": 1, "maximum": 200, "default": 50},
                    },
                    ("query",),
                ),
                ToolAccess("workspace_search", AccessLevel.OBSERVE, RiskClass.READ, True),
                "Use to find definitions, references, requirements, and relevant documentation before editing.",
                ("search", "find", "references", "definitions", "specifications"),
                "path",
            ),
            self._search,
            target_resolver=lambda args: self.resolve(args.get("path", ".")),
        )
        registry.register(
            ToolDefinition(
                "workspace_create_directory",
                "Create a directory inside the open project, including missing parents.",
                _object_schema({"path": path}, ("path",)),
                ToolAccess("workspace_create_directory", AccessLevel.WORKSPACE, RiskClass.WRITE, True),
                "Use to establish a requested project or module layout. It succeeds when the directory already exists.",
                ("create", "directory", "folder", "project", "module"),
                "path",
            ),
            self._create_directory,
            target_resolver=lambda args: self.resolve(args["path"]),
        )
        registry.register(
            ToolDefinition(
                "workspace_create_file",
                "Create a new UTF-8 source, specification, or documentation file without overwriting an existing file.",
                _object_schema(
                    {"path": path, "content": {"type": "string"}},
                    ("path", "content"),
                ),
                ToolAccess("workspace_create_file", AccessLevel.WORKSPACE, RiskClass.WRITE, True),
                "Use for new files. If the file exists, read it and use workspace_replace_text or request workspace_overwrite_file.",
                ("create", "file", "write", "code", "documentation"),
                "path",
            ),
            self._create_file,
            target_resolver=lambda args: self.resolve(args["path"]),
        )
        registry.register(
            ToolDefinition(
                "workspace_replace_text",
                "Replace exact text in an existing UTF-8 project file.",
                _object_schema(
                    {
                        "path": path,
                        "old_text": {"type": "string", "minLength": 1},
                        "new_text": {"type": "string"},
                        "replace_all": {"type": "boolean", "default": False},
                    },
                    ("path", "old_text", "new_text"),
                ),
                ToolAccess("workspace_replace_text", AccessLevel.WORKSPACE, RiskClass.WRITE, True),
                "Use for focused edits after reading the file. By default the old text must occur exactly once, preventing ambiguous edits.",
                ("edit", "replace", "patch", "code", "documentation"),
                "path",
            ),
            self._replace_text,
            target_resolver=lambda args: self.resolve(args["path"]),
        )
        registry.register(
            ToolDefinition(
                "workspace_overwrite_file",
                "Replace the complete contents of an existing UTF-8 project file.",
                _object_schema(
                    {"path": path, "content": {"type": "string"}},
                    ("path", "content"),
                ),
                ToolAccess(
                    "workspace_overwrite_file",
                    AccessLevel.WORKSPACE,
                    RiskClass.WRITE,
                    True,
                    always_requires_approval=True,
                ),
                "Use only when a complete rewrite is genuinely required. This tool always requires exact user approval.",
                ("overwrite", "rewrite", "file", "approval"),
                "path",
            ),
            self._overwrite_file,
            target_resolver=lambda args: self.resolve(args["path"]),
        )

    def _list(self, args: Mapping[str, Any]) -> ToolOutput:
        root = self.resolve(args.get("path", "."))
        if not root.is_dir():
            raise ValueError(f"directory does not exist: {root}")
        recursive = bool(args.get("recursive", False))
        limit = min(max(int(args.get("limit", 200)), 1), 1000)
        iterator = root.rglob("*") if recursive else root.iterdir()
        entries: list[dict[str, Any]] = []
        for item in iterator:
            if any(part in IGNORED_DIRECTORIES for part in item.relative_to(root).parts):
                continue
            entries.append(
                {
                    "path": self.display_path(item),
                    "kind": "directory" if item.is_dir() else "file",
                    "size": item.stat().st_size if item.is_file() else None,
                }
            )
            if len(entries) >= limit:
                break
        entries.sort(key=lambda item: (item["kind"] != "directory", item["path"].lower()))
        return ToolOutput(f"Listed {len(entries)} entries.", {"entries": entries})

    def _read(self, args: Mapping[str, Any]) -> ToolOutput:
        file = self.resolve(args["path"])
        if not file.is_file():
            raise ValueError(f"file does not exist: {file}")
        if file.stat().st_size > 2_000_000:
            raise ValueError("file exceeds the 2 MB read limit")
        lines = file.read_text(encoding="utf-8").splitlines()
        start = max(int(args.get("start_line", 1)), 1)
        end = min(int(args.get("end_line", start + 399)), len(lines))
        if end < start:
            raise ValueError("end_line must not be before start_line")
        selected = [f"{index:>6}  {lines[index - 1]}" for index in range(start, end + 1)]
        return ToolOutput(
            f"Read lines {start}-{end} of {len(lines)}.",
            {"path": self.display_path(file), "text": "\n".join(selected), "total_lines": len(lines)},
        )

    def _search(self, args: Mapping[str, Any]) -> ToolOutput:
        query = str(args["query"])
        root = self.resolve(args.get("path", "."))
        pattern = str(args.get("file_pattern", "*"))
        limit = min(max(int(args.get("limit", 50)), 1), 200)
        if not root.is_dir():
            raise ValueError(f"directory does not exist: {root}")
        matches: list[dict[str, Any]] = []
        for file in root.rglob("*"):
            if not file.is_file() or file.stat().st_size > 2_000_000:
                continue
            relative = file.relative_to(root)
            if any(part in IGNORED_DIRECTORIES for part in relative.parts) or not fnmatch.fnmatch(file.name, pattern):
                continue
            try:
                lines = file.read_text(encoding="utf-8").splitlines()
            except (UnicodeError, OSError):
                continue
            for line_number, line in enumerate(lines, 1):
                if query.casefold() not in line.casefold():
                    continue
                matches.append(
                    {
                        "path": self.display_path(file),
                        "line": line_number,
                        "text": line.strip()[:500],
                    }
                )
                if len(matches) >= limit:
                    return ToolOutput(f"Found {len(matches)} matches (limit reached).", {"matches": matches})
        return ToolOutput(f"Found {len(matches)} matches.", {"matches": matches})

    def _create_directory(self, args: Mapping[str, Any]) -> ToolOutput:
        directory = self.resolve(args["path"])
        directory.mkdir(parents=True, exist_ok=True)
        relative = self.display_path(directory)
        return ToolOutput(f"Directory is ready: {relative}", changed_paths=(relative,))

    def _create_file(self, args: Mapping[str, Any]) -> ToolOutput:
        file = self.resolve(args["path"])
        if file.exists():
            raise ValueError(f"file already exists: {self.display_path(file)}")
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(str(args["content"]), encoding="utf-8")
        relative = self.display_path(file)
        return ToolOutput(f"Created {relative}.", changed_paths=(relative,))

    def _replace_text(self, args: Mapping[str, Any]) -> ToolOutput:
        file = self.resolve(args["path"])
        if not file.is_file():
            raise ValueError(f"file does not exist: {file}")
        original = file.read_text(encoding="utf-8")
        old = str(args["old_text"])
        new = str(args["new_text"])
        count = original.count(old)
        if count == 0:
            raise ValueError("old_text was not found; read the current file before retrying")
        replace_all = bool(args.get("replace_all", False))
        if not replace_all and count != 1:
            raise ValueError(f"old_text occurs {count} times; provide more context or set replace_all")
        updated = original.replace(old, new) if replace_all else original.replace(old, new, 1)
        file.write_text(updated, encoding="utf-8")
        relative = self.display_path(file)
        return ToolOutput(f"Replaced {count if replace_all else 1} occurrence(s) in {relative}.", changed_paths=(relative,))

    def _overwrite_file(self, args: Mapping[str, Any]) -> ToolOutput:
        file = self.resolve(args["path"])
        if not file.is_file():
            raise ValueError("workspace_overwrite_file only replaces an existing file")
        file.write_text(str(args["content"]), encoding="utf-8")
        relative = self.display_path(file)
        return ToolOutput(f"Overwrote {relative}.", changed_paths=(relative,))


class ProcessToolset:
    def __init__(self, root: str | Path, allowed_executables: Sequence[str]) -> None:
        self.root = Path(root).resolve(strict=False)
        self.allowed = {name.casefold() for name in allowed_executables}

    def _resolve_cwd(self, value: Any) -> Path:
        supplied = Path(str(value or "."))
        return supplied.resolve(strict=False) if supplied.is_absolute() else (self.root / supplied).resolve(strict=False)

    def register(self, registry: ToolRegistry) -> None:
        registry.register(
            ToolDefinition(
                "engineer_run_process",
                "Run an allow-listed build, test, compiler, or diagnostic process without a command shell.",
                _object_schema(
                    {
                        "command": {
                            "type": "array",
                            "items": {"type": "string"},
                            "minItems": 1,
                            "description": "Executable and arguments as separate values; shell syntax is not accepted.",
                        },
                        "cwd": {"type": "string", "default": "."},
                        "timeout_seconds": {"type": "integer", "minimum": 1, "maximum": 600, "default": 120},
                    },
                    ("command",),
                ),
                ToolAccess("engineer_run_process", AccessLevel.ENGINEER, RiskClass.EXECUTE, True),
                "Use after edits to run the host-approved compiler, build, test, or diagnostic command. Inspect the captured exit code and output before deciding the next action.",
                ("build", "test", "compile", "diagnostics", "debug", "process"),
                "cwd",
            ),
            self._run,
            target_resolver=lambda args: self._resolve_cwd(args.get("cwd", ".")),
        )

    def _run(self, args: Mapping[str, Any]) -> ToolOutput:
        command = args.get("command")
        if not isinstance(command, list) or not command or not all(isinstance(item, str) for item in command):
            raise ValueError("command must be a non-empty array of strings")
        executable = Path(command[0]).name.casefold()
        if executable not in self.allowed:
            raise ValueError(f"executable is not allowed by this host: {command[0]}")
        cwd = self._resolve_cwd(args.get("cwd", "."))
        if not cwd.is_dir():
            raise ValueError(f"working directory does not exist: {cwd}")
        timeout = min(max(int(args.get("timeout_seconds", 120)), 1), 600)
        try:
            completed = subprocess.run(
                command,
                cwd=cwd,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
                shell=False,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            raise ValueError(f"process exceeded the {timeout}-second timeout") from error
        stdout = completed.stdout[-50_000:]
        stderr = completed.stderr[-50_000:]
        return ToolOutput(
            f"Process exited with code {completed.returncode}.",
            {"exit_code": completed.returncode, "stdout": stdout, "stderr": stderr},
        )


def create_engineering_registry(
    root: str | Path,
    *,
    allowed_executables: Sequence[str] = (),
) -> ToolRegistry:
    registry = ToolRegistry()
    WorkspaceToolset(root).register(registry)
    if allowed_executables:
        ProcessToolset(root, allowed_executables).register(registry)
    return registry
