"""Focused live-app acceptance. Run create, restart DEWB, reopen, then cleanup.
Uses the same local agent API and live main-library widget as normal app actions.
No solvers or other application tests are run. The state directory is test-owned.
"""
import argparse, base64, hashlib, json, os, urllib.request, urllib.error
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("phase", choices=["create", "reopen", "cleanup"])
parser.add_argument("directory", type=Path)
args = parser.parse_args()
args.directory.mkdir(parents=True, exist_ok=True)
statefile = args.directory / "acceptance-state.json"
appdata = Path(os.environ["APPDATA"]) / "DjehutiElectronicsLab"
blocks = appdata / "user_library" / "blocks"
discovery = json.loads((appdata / "agent-api.json").read_text(encoding="utf-8-sig"))
results = []

def tool(name, **arguments):
    data = json.dumps({"name": name, "arguments": arguments}).encode()
    req = urllib.request.Request(discovery["baseUrl"] + "/v1/tools/call", data,
        {"Authorization": "Bearer " + discovery["token"], "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            result = json.load(response)["result"]
    except urllib.error.HTTPError as e:
        raise AssertionError(f"{name}: {e.read().decode()}") from e
    results.append({"tool": name, "arguments": arguments, "result": result})
    assert result is not None and result.get("ok", True), f"{name}: {result}"
    return result

def manage(action, name="", value=""):
    return tool("library_manage_user", action=action, name=name, value=value)

def rows():
    return tool("library_get_hierarchy")["state"]["visibleEntries"]

def circuit():
    result = tool("circuit_inspect")
    return result.get("circuit", result)

def snapshot_file(file):
    return base64.b64encode(file.read_bytes()).decode() if file.exists() else None

def restore_file(file, value):
    if value is None:
        if file.exists(): file.unlink()
    else: file.write_bytes(base64.b64decode(value))

try:
    if args.phase == "create":
        assert not statefile.exists(), "Use a new test directory"
        assert not any(e["name"] == "Signal Pass-Through" for e in tool("library_list_blocks")["blocks"]), "Preserve existing component; choose an isolated library for this acceptance"
        state = {"hashes": {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in blocks.glob("*.block.json")},
                 "categories": snapshot_file(blocks / "categories.json"),
                 "recent": snapshot_file(appdata / "recent_projects.json")}
        statefile.write_text(json.dumps(state), encoding="utf-8")
        tool("project_create", name="User Library Acceptance", location=str(args.directory.resolve()))
        tool("diagram_create", name="Source")
        block = tool("schematic_subdiagram_create", name="Signal Pass-Through", x=240, y=192)
        tool("schematic_subdiagram_add_port", block="Signal Pass-Through", name="Input", side="left")
        tool("schematic_subdiagram_add_port", block="Signal Pass-Through", name="Output", side="right")
        tool("schematic_subdiagram_open", target="Signal Pass-Through")
        c = circuit()
        state["source_circuit"] = c
        statefile.write_text(json.dumps(state), encoding="utf-8")
        ports = [c for c in c["components"] if c["symbol"] == "block_port"]
        assert len(ports) == 2
        # Port bubbles expose their one pin (1) through the authoritative circuit.
        tool("schematic_connect", a=ports[0]["id"] + ".1", b=ports[1]["id"] + ".1")
        tool("schematic_subdiagram_open", target="main")
        saved = tool("library_save_block", block="Signal Pass-Through", name="Signal Pass-Through")
        state["saved"] = saved
        state["source_circuit"] = circuit()
        statefile.write_text(json.dumps(state), encoding="utf-8")
        tool("library_search", query="")
        tool("library_set_expanded", category="User Defined", expanded=True)
        assert any(r.get("name") == "Signal Pass-Through" and r["category"] == "User Defined" and r["subcategory"] == "" for r in rows())
        tool("library_search", query="signal pass-through")
        assert sum(r.get("name") == "Signal Pass-Through" for r in rows()) == 1
        tool("library_select", symbolId="user_block:Signal Pass-Through")
        assert tool("library_get_selected")["symbolId"] == "user_block:Signal Pass-Through"
        tool("diagram_create", name="Destination")
        tool("schematic_place_symbol", symbolId="user_block:Signal Pass-Through", x=240, y=192)
        tool("schematic_place_symbol", symbolId="user_block:Signal Pass-Through", x=528, y=192)
        c = circuit()
        state["destination"] = c
        statefile.write_text(json.dumps(state), encoding="utf-8")
        copies = [v for v in c["components"] if v["symbol"] == "sub_block"]
        assert len(copies) == 2 and len({v["childSheet"] for v in copies}) == 2
        assert len({v["id"] for v in c["components"]}) == len(c["components"])
        source = next(v for v in state["source_circuit"]["components"] if v["symbol"] == "sub_block")
        for copy in copies:
            assert copy["ports"] == source["ports"], "Pin names and stable identities preserved"
            internal = [v for v in c["components"] if v.get("sheet") == copy["childSheet"]]
            assert len(internal) == 2 and all(v["symbol"] == "block_port" for v in internal)
            ids = {v["id"] for v in internal}
            assert sum(w["a"].split(".")[0] in ids and w["b"].split(".")[0] in ids for w in c["wires"]) == 1
        manage("create_category", "DEWB Acceptance Temporary")
        manage("move_component", "Signal Pass-Through", "DEWB Acceptance Temporary")
        manage("rename_category", "DEWB Acceptance Temporary", "DEWB Acceptance Renamed")
        tool("diagram_save")
    elif args.phase == "reopen":
        state = json.loads(statefile.read_text(encoding="utf-8"))
        tool("library_search", query="signal pass-through")
        assert any(r.get("name") == "Signal Pass-Through" and r["subcategory"] == "DEWB Acceptance Renamed" for r in rows())
        tool("project_open", project=str((args.directory / "User Library Acceptance").resolve()))
        tool("diagram_open", name="Destination")
        assert circuit()["components"] == state["destination"]["components"]
        tool("library_place_block", name="Signal Pass-Through", x=816, y=192)
        assert len([c for c in circuit()["components"] if c["symbol"] == "sub_block"]) == 3
        before = circuit()
        manage("rename_component", "Signal Pass-Through", "DEWB Acceptance Renamed Component")
        manage("delete_component", "DEWB Acceptance Renamed Component")
        assert circuit() == before, "Managing templates must never edit inserted copies"
        manage("delete_category", "DEWB Acceptance Renamed")
        tool("library_search", query="resistor")
        assert any(r.get("id") == "resistor" for r in rows())
        tool("library_select", symbolId="resistor")
        tool("schematic_place_symbol", symbolId="resistor", x=240, y=480)
        tool("diagram_save")
    else:
        state = json.loads(statefile.read_text(encoding="utf-8"))
        for entry in tool("library_list_blocks")["blocks"]:
            if entry["name"] in ("Signal Pass-Through", "DEWB Acceptance Renamed Component"):
                manage("delete_component", entry["name"])
        for category in ("DEWB Acceptance Temporary", "DEWB Acceptance Renamed"):
            # Existing categories are never touched; these names belong to this run.
            if any(r["subcategory"] == category for r in rows()): manage("delete_category", category)
        assert {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in blocks.glob("*.block.json")} == state["hashes"], "Existing library content changed"
        restore_file(blocks / "categories.json", state["categories"])
        restore_file(appdata / "recent_projects.json", state["recent"])
        manage("refresh")
    print("PASS:", args.phase)
finally:
    (args.directory / (args.phase + "-results.json")).write_text(json.dumps(results, indent=2), encoding="utf-8")
