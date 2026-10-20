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
        except Exception as e:
            print("Could not remove old api_file:", e)

    exe_path = os.path.abspath('prototype/bin/Debug/djehuti_electronics_lab.exe')
    print(f"Launching {exe_path}...")
    proc = subprocess.Popen([exe_path], cwd=os.path.dirname(exe_path))

    base_url = None
    token = None
    for i in range(30):
        time.sleep(1)
        if os.path.exists(api_file):
            try:
                with open(api_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    token = data.get('token')
                    base_url = data.get('baseUrl')
                    if token and base_url:
                        print(f"Connected to agent API at {base_url} (token: {token[:6]}...)")
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
            return None
        except Exception as e:
            print(f"Tool call error ({tool_name}): {e}")
            return None

    try:
        time.sleep(1)

        # Step 1: Initial state - must be collapsed
        print("\n--- Step 1: Initial State Check ---")
        res0 = call_tool("library_get_hierarchy")
        assert res0 and res0.get("ok"), "library_get_hierarchy failed"
        st0 = res0.get("state", {})
        assert len(st0.get("expandedCategories", [])) == 0, f"Expected 0 expanded categories, got {st0.get('expandedCategories')}"
        assert len(st0.get("expandedSubcategories", [])) == 0, f"Expected 0 expanded subcategories, got {st0.get('expandedSubcategories')}"
        visible_entries = st0.get("visibleEntries", [])
        assert all(r.get("level") == 0 for r in visible_entries), "Non-level-0 entry visible on startup!"
        print(f"PASS: App started with all {len(visible_entries)} categories collapsed!")

        # Step 2: Expand All
        print("\n--- Step 2: Expand All Check ---")
        exp_all = call_tool("library_expand_all")
        print("Expand all result:", exp_all)
        assert exp_all and exp_all.get("ok"), "library_expand_all failed"
        res1 = call_tool("library_get_hierarchy")
        st1 = res1.get("state", {})
        entries1 = st1.get("visibleEntries", [])
        levels1 = set(r.get("level") for r in entries1)
        print(f"Total rows after expand all: {len(entries1)}, levels present: {levels1}")
        assert {0, 1, 2}.issubset(levels1), f"Expected levels 0, 1, 2 visible after expand all, got {levels1}"
        print(f"PASS: Expand All expanded all {len(entries1)} entries across levels {levels1}!")

        # Step 3: Collapse All
        print("\n--- Step 3: Collapse All Check ---")
        col_all = call_tool("library_collapse_all")
        print("Collapse all result:", col_all)
        assert col_all and col_all.get("ok"), "library_collapse_all failed"
        res2 = call_tool("library_get_hierarchy")
        st2 = res2.get("state", {})
        entries2 = st2.get("visibleEntries", [])
        assert all(r.get("level") == 0 for r in entries2), f"Expected only level 0 after collapse all"
        print(f"PASS: Collapse All collapsed everything back to {len(entries2)} top-level categories!")

        # Step 4: Nested Expansion (Category then Subcategory)
        print("\n--- Step 4: Nested Expansion Check ---")
        cat_exp = call_tool("library_set_expanded", {"category": "Passives", "expanded": True})
        print("Expand Passives result:", cat_exp)
        res3 = call_tool("library_get_hierarchy")
        st3 = res3.get("state", {})
        passives_entries = [r for r in st3.get("visibleEntries", []) if r.get("category") == "Passives"]
        print("Passives rows visible:", [f"lvl{r.get('level')}:{r.get('subcategory') or r.get('category')}" for r in passives_entries])
        # Resistors subcategory should now be visible at level 1
        assert any(r.get("level") == 1 and r.get("subcategory") == "Resistors" for r in passives_entries), "Resistors subcat not visible under Passives"
        
        # Now expand Resistors subcategory
        subcat_exp = call_tool("library_set_expanded", {"category": "Passives", "subcategory": "Resistors", "expanded": True})
        print("Expand Passives/Resistors result:", subcat_exp)
        res4 = call_tool("library_get_hierarchy")
        st4 = res4.get("state", {})
        resistor_items = [r for r in st4.get("visibleEntries", []) if r.get("subcategory") == "Resistors" and r.get("level") == 2]
        print("Resistors items visible:", [r.get("id") for r in resistor_items])
        assert len(resistor_items) > 0, "Expected resistor items to be visible after expanding subcategory"
        print("PASS: Nested expansion (Category -> Subcategory -> Component) works!")

        # Step 5: Search
        print("\n--- Step 5: Search Check ---")
        s1 = call_tool("library_search", {"query": "resistor"})
        print("Search 'resistor' result:", s1)
        res5 = call_tool("library_get_hierarchy")
        st5 = res5.get("state", {})
        search_entries = st5.get("visibleEntries", [])
        print("Search visible rows:", [f"lvl{r.get('level')}:{r.get('id') or r.get('subcategory') or r.get('category')}" for r in search_entries])
        assert any(r.get("id") == "resistor" for r in search_entries), "Expected resistor in search results"
        print("PASS: Search filters and expands matching hierarchy!")

        # Step 6: Clear Search and restore previous session expansion
        print("\n--- Step 6: Search Restore Check ---")
        s2 = call_tool("library_search", {"query": ""})
        res6 = call_tool("library_get_hierarchy")
        st6 = res6.get("state", {})
        print("Restored visible categories/subcategories:", st6.get("expandedCategories"), st6.get("expandedSubcategories"))
        assert "Passives" in st6.get("expandedCategories", []), "Expected 'Passives' to still be expanded after clearing search"
        assert "Passives/Resistors" in st6.get("expandedSubcategories", []), "Expected 'Passives/Resistors' to still be expanded after clearing search"
        print("PASS: Clearing search restored user's previous expansion state!")

        # Step 7: Placement
        print("\n--- Step 7: Placement Check ---")
        place_res = call_tool("schematic_place_symbol", {"symbolId": "resistor", "x": 140, "y": 140})
        print("Place resistor result:", place_res)
        assert place_res and place_res.get("ok"), "Failed to place component"
        print("PASS: Component placement verified!")

        # Step 8: Screen capture
        print("\n--- Step 8: Screenshot ---")
        time.sleep(1)
        shot_script = """
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bitmap = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($screen.Location, [System.Drawing.Point]::Empty, $screen.Size)
$bitmap.Save("prototype/gate_a_screenshot.png", [System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
"""
        subprocess.run(["powershell", "-Command", shot_script], check=True)
        print("Screenshot saved to prototype/gate_a_screenshot.png")

        print("\n==========================================")
        print("ALL ACCEPTANCE GATE A CRITERIA PASSED 100%")
        print("==========================================")
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
