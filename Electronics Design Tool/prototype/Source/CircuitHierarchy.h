#pragma once
#include <string>
#include <vector>

// The diagram's sheet hierarchy as an ordered tree, for the Circuit Hierarchy
// panel. Pure data so it can be tested without the UI.
namespace circuit_hierarchy
{
struct Sheet
{
    std::string id;     // sub-diagram sheet id ("" = the top-level sheet)
    std::string name;   // what the user calls it
    std::string parent; // sheet the sub_block sits on
};

struct Row
{
    std::string id;
    std::string name;
    int depth = 0;              // 0 = top level
    bool missingParent = false; // parent sheet not found, or part of a cycle; listed under the top level
};

// Top level first, then every sheet depth-first under its parent, siblings in
// input order. Sheets that cannot be reached from the top level are still
// listed (under it, flagged) so nothing in the diagram is unreachable.
std::vector<Row> flatten(const std::vector<Sheet>& sheets, const std::string& rootName);
}
