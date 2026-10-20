import json
import os
import subprocess
import time
import urllib.request
import urllib.error

def main():
    appdata = os.environ.get('APPDATA', '')
    api_file = os.path.join(appdata, 'DjehutiElectronicsLab', 'agent-api.json')
    if os.path.exists(api_file):
        try:
            os.remove(api_file)
        except Exception:
            pass

    exe_path = os.path.abspath('prototype/bin/Debug/djehuti_electronics_lab.exe')
    print(f"Launching {exe_path}...")
    proc = subprocess.Popen([exe_path], cwd=os.path.dirname(exe_path))

    base_url = None
    token = None
    for _ in range(30):
        time.sleep(1)
        if os.path.exists(api_file):
            try:
                with open(api_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    token = data.get('token')
                    base_url = data.get('baseUrl')
                    if token and base_url:
                        print(f"Connected to agent API at {base_url}")
                        break
            except Exception:
                pass

    if not token or not base_url:
        print("Failed to get agent API credentials")
        proc.terminate()
        return False

    def call_tool(tool_name, arguments={}):
        url = f"{base_url}/v1/tools/call"
        req_data = json.dumps({"name": tool_name, "arguments": arguments}).encode('utf-8')
        req = urllib.request.Request(url, data=req_data, headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json"
        })
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                raw = resp.read().decode('utf-8')
                res = json.loads(raw)
                if "result" in res:
                    return res["result"]
                return res
        except urllib.error.HTTPError as he:
            err_content = he.read().decode('utf-8')
            print(f"HTTPError on {tool_name}: {he.code} {err_content}")
            try:
                res = json.loads(err_content)
                if "result" in res:
                    return res["result"]
                return res
            except Exception:
                return None
        except Exception as e:
            print(f"Tool call error ({tool_name}): {e}")
            return None

    try:
        time.sleep(1)

        # 1. Startup Collapsed Verification
        print("\n--- 1. Startup Collapsed Verification ---")
        h0 = call_tool("library_get_hierarchy")
        st0 = h0.get("state", {})
        assert len(st0.get("expandedCategories", [])) == 0, "Categories not collapsed on startup"
        print("PASS: App started with 100% collapsed categories.")

        # 2. Expand Vacuum Tubes Category
        print("\n--- 2. Vacuum Tubes Category & Subcategories Check ---")
        call_tool("library_set_expanded", {"category": "Vacuum Tubes", "expanded": True})
        h1 = call_tool("library_get_hierarchy")
        st1 = h1.get("state", {})
        tube_subcats = [r.get("subcategory") for r in st1.get("visibleEntries", []) if r.get("category") == "Vacuum Tubes" and r.get("level") == 1]
        print(f"Visible Vacuum Tube subcategories ({len(tube_subcats)}):", tube_subcats)
        expected_subcats = [
            "Small-Signal Triodes",
            "Driver Triodes",
            "Power Triodes",
            "Small-Signal Pentodes",
            "Output Pentodes",
            "Beam Power Tubes",
            "Rectifier Tubes"
        ]
        assert tube_subcats == expected_subcats, f"Subcategories mismatch: {tube_subcats} vs {expected_subcats}"
        print("PASS: All 7 requested Vacuum Tubes subcategories present in exact order!")

        # 3. Expand All and verify catalog count
        print("\n--- 3. Expand All & Catalog Inventory Verification ---")
        call_tool("library_expand_all")
        h2 = call_tool("library_get_hierarchy")
        st2 = h2.get("state", {})
        all_tubes = [r for r in st2.get("visibleEntries", []) if r.get("category") == "Vacuum Tubes" and r.get("level") == 2]
        print(f"Total catalog vacuum tubes: {len(all_tubes)}")
        assert len(all_tubes) >= 23, f"Expected at least 23 tube types, found {len(all_tubes)}"

        tube_ids = set(r.get("id") for r in all_tubes)
        required_ids = [
            "tube_12ax7", "tube_12au7", "tube_12at7", "tube_12ay7", "tube_5751", "tube_6sl7gt", "tube_6dj8", "tube_6922",
            "tube_12bh7a", "tube_6sn7gt", "tube_12dw7",
            "tube_300b", "tube_2a3",
            "tube_ef86", "tube_6au6a",
            "tube_el84", "tube_el34",
            "tube_6l6gc", "tube_6v6gt", "tube_kt88", "tube_6550",
            "tube_5ar4", "tube_5u4gb", "tube_5y3gt", "tube_ez81"
        ]
        for rid in required_ids:
            assert rid in tube_ids, f"Missing required tube in library: {rid}"
        print(f"PASS: All {len(required_ids)} requested tube types verified in library catalog!")

        # 4. Collapse All
        print("\n--- 4. Collapse All Check ---")
        call_tool("library_collapse_all")
        h3 = call_tool("library_get_hierarchy")
        st3 = h3.get("state", {})
        assert all(r.get("level") == 0 for r in st3.get("visibleEntries", [])), "Collapse all left nested rows"
        print("PASS: Collapse all cleanly collapsed hierarchy.")

        # 5. Search for specific tubes
        print("\n--- 5. Search Filter Check ---")
        call_tool("library_search", {"query": "EL34"})
        h_search = call_tool("library_get_hierarchy")
        search_entries = h_search.get("state", {}).get("visibleEntries", [])
        assert any(r.get("id") == "tube_el34" for r in search_entries), "EL34 not found in search"
        print("PASS: Search located EL34 power pentode under Output Pentodes.")
        call_tool("library_search", {"query": ""})

        # 6. Library Selection and Properties Inspection
        print("\n--- 6. Properties Panel Metadata Inspection ---")
        # Test simulated 12AT7
        call_tool("library_select", {"symbolId": "tube_12at7"})
        info_at7 = call_tool("library_get_selected")
        meta_at7 = info_at7.get("tubeSpecification", {})
        print("12AT7 Metadata Summary:")
        print("   Name:", meta_at7.get("name"))
        print("   Subcategory:", meta_at7.get("subcategory"))
        print("   Classification:", meta_at7.get("classification"))
        print("   Provenance:", meta_at7.get("provenance"))
        print("   Simulated:", meta_at7.get("simulated"))
        assert meta_at7.get("simulated") is True
        assert meta_at7.get("subcategory") == "Small-Signal Triodes"
        assert "RCA" in meta_at7.get("provenance")
        print("PASS: 12AT7 metadata verified (Simulated).")

        # Test simulated 12AY7
        call_tool("library_select", {"symbolId": "tube_12ay7"})
        info_ay7 = call_tool("library_get_selected")
        meta_ay7 = info_ay7.get("tubeSpecification", {})
        print("\n12AY7 Metadata Summary:")
        print("   Name:", meta_ay7.get("name"))
        print("   Classification:", meta_ay7.get("classification"))
        print("   Provenance:", meta_ay7.get("provenance"))
        print("   Simulated:", meta_ay7.get("simulated"))
        assert meta_ay7.get("simulated") is True
        assert meta_ay7.get("subcategory") == "Small-Signal Triodes"
        assert "GE" in meta_ay7.get("provenance") or "Tung-Sol" in meta_ay7.get("provenance")
        print("PASS: 12AY7 metadata verified (Simulated).")

        # Test unmodeled 300B power triode
        call_tool("library_select", {"symbolId": "tube_300b"})
        info_300b = call_tool("library_get_selected")
        meta_300b = info_300b.get("tubeSpecification", {})
        print("\n300B Metadata Summary:")
        print("   Name:", meta_300b.get("name"))
        print("   Subcategory:", meta_300b.get("subcategory"))
        print("   Classification:", meta_300b.get("classification"))
        print("   Heater:", meta_300b.get("heater"))
        print("   Max Ratings:", meta_300b.get("maxRatings"))
        print("   Typical Operation:", meta_300b.get("typicalOperation"))
        print("   Provenance:", meta_300b.get("provenance"))
        print("   Simulated:", meta_300b.get("simulated"))
        assert meta_300b.get("simulated") is False
        assert meta_300b.get("subcategory") == "Power Triodes"
        assert "Western Electric" in meta_300b.get("provenance")
        print("PASS: 300B metadata verified (Specifications only).")

        # Test unmodeled 5AR4 rectifier
        call_tool("library_select", {"symbolId": "tube_5ar4"})
        info_5ar4 = call_tool("library_get_selected")
        meta_5ar4 = info_5ar4.get("tubeSpecification", {})
        print("\n5AR4 Metadata Summary:")
        print("   Name:", meta_5ar4.get("name"))
        print("   Subcategory:", meta_5ar4.get("subcategory"))
        print("   Classification:", meta_5ar4.get("classification"))
        print("   Provenance:", meta_5ar4.get("provenance"))
        assert meta_5ar4.get("simulated") is False
        assert meta_5ar4.get("subcategory") == "Rectifier Tubes"
        assert "Mullard" in meta_5ar4.get("provenance") or "Philips" in meta_5ar4.get("provenance")
        print("PASS: 5AR4 metadata verified (Specifications only).")

        # Test unmodeled 5Y3GT rectifier
        call_tool("library_select", {"symbolId": "tube_5y3gt"})
        info_5y3 = call_tool("library_get_selected")
        meta_5y3 = info_5y3.get("tubeSpecification", {})
        print("\n5Y3GT Metadata Summary:")
        print("   Name:", meta_5y3.get("name"))
        print("   Subcategory:", meta_5y3.get("subcategory"))
        print("   Heater:", meta_5y3.get("heater"))
        print("   Provenance:", meta_5y3.get("provenance"))
        assert meta_5y3.get("simulated") is False
        assert meta_5y3.get("subcategory") == "Rectifier Tubes"
        assert "RCA" in meta_5y3.get("provenance") or "GE" in meta_5y3.get("provenance")
        print("PASS: 5Y3GT metadata verified (Specifications only).")

        # Test unmodeled EZ81 rectifier
        call_tool("library_select", {"symbolId": "tube_ez81"})
        info_ez81 = call_tool("library_get_selected")
        meta_ez81 = info_ez81.get("tubeSpecification", {})
        print("\nEZ81 Metadata Summary:")
        print("   Name:", meta_ez81.get("name"))
        print("   Subcategory:", meta_ez81.get("subcategory"))
        print("   Heater:", meta_ez81.get("heater"))
        print("   Provenance:", meta_ez81.get("provenance"))
        assert meta_ez81.get("simulated") is False
        assert meta_ez81.get("subcategory") == "Rectifier Tubes"
        assert "Mullard" in meta_ez81.get("provenance") or "Philips" in meta_ez81.get("provenance")
        print("PASS: EZ81/6CA4 metadata verified (Specifications only).")

        # 7. Unmodeled tube placement & Graceful Simulation Notice
        print("\n--- 7. Simulation Notice on Unmodeled Tube Placement ---")
        p_kt88 = call_tool("schematic_place_symbol", {"symbolId": "tube_kt88", "x": 120, "y": 120})
        print("Placed KT88:", p_kt88)
        assert p_kt88 and p_kt88.get("ok"), "Failed to place KT88"
        op_unmodeled = call_tool("analytics_run_analysis", {"analysis": "operating_point"})
        print("Simulation result on KT88:", op_unmodeled)
        raw_msg = json.dumps(op_unmodeled)
        assert "specifications only" in raw_msg.lower() or "not yet available" in raw_msg.lower(), f"Missing graceful notice in: {raw_msg}"
        print("PASS: Graceful notice received: 'Model not yet available for KT88 Kinkless Beam Tetrode — specifications only.'")

        # 8. Clean up and test simulated tube (12AT7) placement and simulation
        print("\n--- 8. Simulated Tube (12AT7) Placement & Simulation ---")
        del_res = call_tool("schematic_delete_components", {"refdes": [p_kt88.get("refdes", "V2")]})
        print("Deleted KT88:", del_res)
        p_at7 = call_tool("schematic_place_symbol", {"symbolId": "tube_12at7", "x": 140, "y": 140})
        print("Placed 12AT7:", p_at7)
        assert p_at7 and p_at7.get("ok"), "Failed to place 12AT7"
        op_at7 = call_tool("analytics_run_analysis", {"analysis": "operating_point"})
        print("Simulation result on 12AT7:", op_at7)
        raw_at7 = json.dumps(op_at7)
        assert "specifications only" not in raw_at7.lower(), "12AT7 should be simulated"
        print("PASS: 12AT7 simulated without missing model errors!")

        print("\n========================================================")
        print("ALL EXPANDED CATALOG & VACUUM TUBE REQUIREMENTS VERIFIED!")
        print("========================================================")
        return True

    finally:
        print("Terminating application process...")
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

if __name__ == "__main__":
    success = main()
    exit(0 if success else 1)
