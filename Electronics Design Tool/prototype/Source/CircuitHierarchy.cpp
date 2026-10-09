#include "CircuitHierarchy.h"

#include <functional>
#include <map>
#include <set>

namespace circuit_hierarchy
{
std::vector<Row> flatten(const std::vector<Sheet>& sheets, const std::string& rootName)
{
    std::map<std::string, std::vector<size_t>> children;
    for (size_t i = 0; i < sheets.size(); ++i)
        if (!sheets[i].id.empty())
            children[sheets[i].parent].push_back(i);

    std::vector<Row> rows;
    std::set<std::string> visited { "" };
    rows.push_back({ "", rootName, 0, false });

    std::function<void(const std::string&, int)> walk = [&](const std::string& parent, int depth) {
        const auto found = children.find(parent);
        if (found == children.end())
            return;
        for (const auto index : found->second)
        {
            const auto& sheet = sheets[index];
            if (!visited.insert(sheet.id).second)
                continue;
            rows.push_back({ sheet.id, sheet.name, depth, false });
            walk(sheet.id, depth + 1);
        }
    };
    walk("", 1);

    for (const auto& sheet : sheets)
    {
        if (sheet.id.empty() || !visited.insert(sheet.id).second)
            continue;
        rows.push_back({ sheet.id, sheet.name, 1, true });
        walk(sheet.id, 2);
    }
    return rows;
}
}
