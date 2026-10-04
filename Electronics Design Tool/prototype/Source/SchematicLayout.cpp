#include "SchematicLayout.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>

namespace schematic::layout
{
namespace
{
using P = juce::Point<float>;

enum class Role { Main, Shunt, Supply, Instrument };


struct Ctx
{
    const std::vector<Part>& parts;
    const std::vector<Net>& nets;
    float grid;

    std::vector<Role> role;
    std::vector<int> rotation;
    std::vector<P> position;
    std::vector<bool> placed;
    std::vector<int> layer;
    std::vector<int> netParent;   // part that first reached each net
    std::vector<float> netY;
    std::vector<bool> netHasY;

    // Vertical chains: a stacked part rides on its root part.
    std::vector<int> stackRoot;                   // -1 if not stacked
    std::vector<P> stackOffset;                   // origin relative to the root's origin
    std::map<int, std::vector<int>> stackMembers; // root -> members in chain order
};

float snap(float v, float grid) { return std::round(v / grid) * grid; }

bool isPowerNet(const Ctx& c, int net)
{
    return net >= 0 && c.nets[(size_t)net].kind != NetKind::Signal;
}

bool isSignalNet(const Ctx& c, int net)
{
    return net >= 0 && c.nets[(size_t)net].kind == NetKind::Signal;
}

bool isNegativeSupply(const Ctx& c, int net)
{
    return net >= 0 && c.nets[(size_t)net].kind == NetKind::Supply && c.nets[(size_t)net].name.startsWith("-");
}

// Desired lead direction of a pin on a power net: ground and negative
// supplies sit below the part, positive supplies above (SCH-P3).
P desiredPowerDirection(const Ctx& c, int net)
{
    if (c.nets[(size_t)net].kind == NetKind::Ground || isNegativeSupply(c, net))
        return { 0.0f, 1.0f };
    return { 0.0f, -1.0f };
}

P pinOffset(const Part& part, int pin, int rot) { return rotateOffset(part.symbol.pins[(size_t)pin].offset, rot); }
P pinDir(const Part& part, int pin, int rot) { return rotateOffset(pinLeadDirection(part.symbol, pin), rot); }

int pinCount(const Part& part) { return (int)part.symbol.pins.size(); }

// Rotated footprint including room for the part's labels.
juce::Rectangle<float> footprint(const Part& part, int rot)
{
    auto r = rotateBounds(extentBounds(part.symbol), rot);
    if (isPowerSymbol(part.symbol.id))
        return r;

    // Room for the wire stub (or power symbol) one grid step beyond each
    // pin, so facing pins of neighbours never interleave.
    for (int p = 0; p < pinCount(part); ++p)
    {
        const auto d = pinDir(part, p, rot);
        if (d.y > 0.5f) r = r.withBottom(std::max(r.getBottom(), pinOffset(part, p, rot).y + gridSize));
        if (d.y < -0.5f) r = r.withTop(std::min(r.getY(), pinOffset(part, p, rot).y - gridSize));
        if (d.x > 0.5f) r = r.withRight(std::max(r.getRight(), pinOffset(part, p, rot).x + gridSize));
        if (d.x < -0.5f) r = r.withLeft(std::min(r.getX(), pinOffset(part, p, rot).x - gridSize));
    }

    const auto labels = labelRectsFor(part.symbol, rot);
    // Value text is usually shorter than the full label box.
    const auto trim = [](juce::Rectangle<float> box, juce::Justification j) {
        if (j == juce::Justification::centredLeft) return box.withWidth(64.0f);
        if (j == juce::Justification::centredRight) return box.withLeft(box.getRight() - 64.0f);
        return box.withSizeKeepingCentre(64.0f, box.getHeight());
    };
    return r.getUnion(trim(labels.refdes, labels.justification)).getUnion(trim(labels.value, labels.justification));
}

bool isSourceSymbol(const juce::String& id)
{
    return id == "ac_voltage_source" || id == "signal_source" || id == "current_source"
        || id == "ac_current_source" || id == "voltage_source" || id == "battery"
        || id.startsWith("connector_");
}


bool naturallyHorizontal(const Part& part)
{
    return pinCount(part) == 2
        && std::abs(part.symbol.pins[0].offset.y - part.symbol.pins[1].offset.y) < 0.5f;
}

// Chooses the 90-degree rotation that points power pins where SCH-P3 wants
// them; ties keep the default orientation.
int bestPowerRotation(const Ctx& c, int index)
{
    const auto& part = c.parts[(size_t)index];
    int best = 0;
    float bestScore = -1.0f;
    for (int rot : { 0, 90, 180, 270 })
    {
        float score = 0.0f;
        for (int p = 0; p < pinCount(part); ++p)
        {
            const auto net = part.pinNets[(size_t)p];
            if (!isPowerNet(c, net))
                continue;
            const auto d = pinDir(part, p, rot);
            const auto want = desiredPowerDirection(c, net);
            score += d.x * want.x + d.y * want.y;
        }
        if (score > bestScore + 0.01f)
        {
            bestScore = score;
            best = rot;
        }
    }
    return best;
}

void classify(Ctx& c)
{
    const auto n = c.parts.size();
    c.role.assign(n, Role::Main);
    for (size_t i = 0; i < n; ++i)
    {
        const auto& part = c.parts[i];
        int signalPins = 0, powerPins = 0;
        for (auto net : part.pinNets)
        {
            if (isSignalNet(c, net)) ++signalPins;
            else if (isPowerNet(c, net)) ++powerPins;
        }

        if (isInstrumentSymbol(part.symbol.id))
            c.role[i] = Role::Instrument;
        else if ((part.symbol.id == "voltage_source" || part.symbol.id == "battery") && signalPins == 0)
            c.role[i] = Role::Supply;
        else if (pinCount(part) == 2 && signalPins == 1 && powerPins == 1 && !isSourceSymbol(part.symbol.id))
            c.role[i] = Role::Shunt;
    }

    // A net reached only by shunts has nothing to hang them from; promote one.
    for (size_t net = 0; net < c.nets.size(); ++net)
    {
        if (c.nets[net].kind != NetKind::Signal)
            continue;
        int firstShunt = -1;
        bool hasMain = false;
        for (size_t i = 0; i < n; ++i)
        {
            const auto& pins = c.parts[i].pinNets;
            if (std::find(pins.begin(), pins.end(), (int)net) == pins.end())
                continue;
            if (c.role[i] == Role::Main) hasMain = true;
            if (c.role[i] == Role::Shunt && firstShunt < 0) firstShunt = (int)i;
        }
        if (!hasMain && firstShunt >= 0)
            c.role[(size_t)firstShunt] = Role::Main;
    }
}

std::vector<int> partsOnNet(const Ctx& c, int net)
{
    std::vector<int> result;
    for (int i = 0; i < (int)c.parts.size(); ++i)
    {
        const auto& pins = c.parts[(size_t)i].pinNets;
        if (std::find(pins.begin(), pins.end(), net) != pins.end())
            result.push_back(i);
    }
    return result;
}

// Breadth-first layering of main parts over signal nets, one connected
// component at a time, starting from input sources (leftmost first).
std::vector<std::vector<int>> layerComponents(Ctx& c)
{
    const auto n = (int)c.parts.size();
    c.layer.assign((size_t)n, -1);
    c.netParent.assign(c.nets.size(), -1);

    std::vector<int> order;
    for (int i = 0; i < n; ++i)
        if (c.role[(size_t)i] == Role::Main)
            order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto sa = isSourceSymbol(c.parts[(size_t)a].symbol.id) || c.parts[(size_t)a].pinnedColumn < 0;
        const auto sb = isSourceSymbol(c.parts[(size_t)b].symbol.id) || c.parts[(size_t)b].pinnedColumn < 0;
        if (sa != sb) return sa;
        return c.parts[(size_t)a].originalPosition.x < c.parts[(size_t)b].originalPosition.x;
    });

    std::vector<std::vector<int>> components;
    for (int root : order)
    {
        if (c.layer[(size_t)root] >= 0)
            continue;
        // Find the whole connected component first, then walk it breadth
        // first from all of its inputs at once, so every input sits in the
        // first layer (multi-input circuits, sub-diagram port bubbles).
        std::vector<int> members;
        {
            std::set<int> seen { root };
            std::deque<int> pending { root };
            while (!pending.empty())
            {
                const auto current = pending.front();
                pending.pop_front();
                members.push_back(current);
                for (auto net : c.parts[(size_t)current].pinNets)
                    if (isSignalNet(c, net))
                        for (int next : partsOnNet(c, net))
                            if (c.role[(size_t)next] == Role::Main && seen.insert(next).second)
                                pending.push_back(next);
            }
        }
        auto isInput = [&](int part) {
            return isSourceSymbol(c.parts[(size_t)part].symbol.id) || c.parts[(size_t)part].pinnedColumn < 0;
        };
        std::deque<int> queue;
        for (int part : order) // keeps the left-to-right preference among inputs
            if (std::find(members.begin(), members.end(), part) != members.end() && isInput(part))
            {
                c.layer[(size_t)part] = 0;
                queue.push_back(part);
            }
        if (queue.empty())
        {
            c.layer[(size_t)root] = 0;
            queue.push_back(root);
        }

        std::vector<int> component;
        while (!queue.empty())
        {
            const auto current = queue.front();
            queue.pop_front();
            component.push_back(current);
            for (auto net : c.parts[(size_t)current].pinNets)
            {
                if (!isSignalNet(c, net))
                    continue;
                if (c.netParent[(size_t)net] < 0)
                    c.netParent[(size_t)net] = current;
                for (int next : partsOnNet(c, net))
                {
                    if (c.role[(size_t)next] != Role::Main || c.layer[(size_t)next] >= 0)
                        continue;
                    c.layer[(size_t)next] = c.layer[(size_t)current] + 1;
                    queue.push_back(next);
                }
            }
        }
        components.push_back(component);
    }

    // Pinned parts (sub-diagram port bubbles): inputs in the first layer,
    // outputs one layer past everything else in their component.
    for (const auto& component : components)
    {
        int last = 0;
        for (int part : component)
            if (c.parts[(size_t)part].pinnedColumn == 0)
                last = std::max(last, c.layer[(size_t)part]);
        for (int part : component)
        {
            if (c.parts[(size_t)part].pinnedColumn < 0) c.layer[(size_t)part] = 0;
            if (c.parts[(size_t)part].pinnedColumn > 0) c.layer[(size_t)part] = last + 1;
        }
    }
    return components;
}

void orient(Ctx& c)
{
    c.rotation.assign(c.parts.size(), 0);
    for (int i = 0; i < (int)c.parts.size(); ++i)
    {
        const auto& part = c.parts[(size_t)i];
        const auto role = c.role[(size_t)i];
        if (part.fixedRotation >= 0)
        {
            c.rotation[(size_t)i] = part.fixedRotation;
            continue;
        }
        if (role == Role::Instrument || pinCount(part) != 2)
            continue;

        if (role == Role::Shunt || role == Role::Supply)
        {
            c.rotation[(size_t)i] = bestPowerRotation(c, i);
            continue;
        }

        if (naturallyHorizontal(part) && !isSourceSymbol(part.symbol.id))
        {
            // Input pin on the left: the net driven from the earliest layer.
            int inputPin = 0;
            int bestLayer = std::numeric_limits<int>::max();
            for (int p = 0; p < 2; ++p)
            {
                const auto net = part.pinNets[(size_t)p];
                if (!isSignalNet(c, net))
                    continue;
                const auto parent = c.netParent[(size_t)net];
                if (parent >= 0 && parent != i && c.layer[(size_t)parent] < bestLayer)
                {
                    bestLayer = c.layer[(size_t)parent];
                    inputPin = p;
                }
            }
            c.rotation[(size_t)i] = inputPin == 0 ? 0 : 180;
            continue;
        }

        c.rotation[(size_t)i] = bestPowerRotation(c, i);
    }
}

// Order-preserving 1D overlap removal: items keep their sorted order and
// each block of touching items sits at the mean of its members' wishes.
struct Item
{
    int part;
    float desired;   // desired origin y
    float top;       // footprint top relative to origin (negative)
    float bottom;    // footprint bottom relative to origin
};

std::vector<float> separate(std::vector<Item>& items, float gap)
{
    // Order by where each part's anchor pin wants to be, not by footprint
    // centre: a stack hanging off one side must not flip the order.
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.desired < b.desired;
    });

    struct Block { std::vector<size_t> members; std::vector<float> offsets; float position; };
    std::vector<Block> blocks;
    for (size_t i = 0; i < items.size(); ++i)
    {
        blocks.push_back({ { i }, { 0.0f }, items[i].desired });
        while (blocks.size() >= 2)
        {
            auto& last = blocks.back();
            auto& prev = blocks[blocks.size() - 2];
            const auto prevEnd = prev.position + prev.offsets.back() + items[prev.members.back()].bottom;
            const auto lastStart = last.position + items[last.members.front()].top;
            if (lastStart >= prevEnd + gap)
                break;
            // Merge last into prev.
            const auto base = prev.offsets.back() + items[prev.members.back()].bottom + gap - items[last.members.front()].top;
            for (size_t k = 0; k < last.members.size(); ++k)
            {
                prev.members.push_back(last.members[k]);
                prev.offsets.push_back(base + last.offsets[k]);
            }
            float sum = 0.0f;
            for (size_t k = 0; k < prev.members.size(); ++k)
                sum += items[prev.members[k]].desired - prev.offsets[k];
            prev.position = sum / (float)prev.members.size();
            blocks.pop_back();
        }
    }

    std::vector<float> result(items.size());
    for (const auto& block : blocks)
        for (size_t k = 0; k < block.members.size(); ++k)
            result[block.members[k]] = block.position + block.offsets[k];
    return result;
}

// Desired origin y so that `pin` sits one grid step off a horizontal net
// line at netY (exactly on it for horizontal pins).
float originForPinOnNet(const Ctx& c, int part, int pin, float netY)
{
    const auto rot = c.rotation[(size_t)part];
    const auto offset = pinOffset(c.parts[(size_t)part], pin, rot);
    const auto dir = pinDir(c.parts[(size_t)part], pin, rot);
    const auto pinY = netY - dir.y * c.grid;
    return pinY - offset.y;
}

void recordNetY(Ctx& c, int part)
{
    const auto& p = c.parts[(size_t)part];
    const auto rot = c.rotation[(size_t)part];
    for (int pin = 0; pin < pinCount(p); ++pin)
    {
        const auto net = p.pinNets[(size_t)pin];
        if (!isSignalNet(c, net) || c.netHasY[(size_t)net])
            continue;
        const auto at = c.position[(size_t)part] + pinOffset(p, pin, rot);
        const auto dir = pinDir(p, pin, rot);
        c.netY[(size_t)net] = at.y + dir.y * c.grid;
        c.netHasY[(size_t)net] = true;
    }
}

struct Column
{
    float key;
    std::vector<int> parts;
};

int terminalCount(const Ctx& c, int net)
{
    int count = 0;
    for (const auto& part : c.parts)
        for (auto n : part.pinNets)
            if (n == net)
                ++count;
    return count;
}

// A two-pin part whose input net joins only it and one vertical pin of an
// earlier part (an emitter or collector, say) stands in line with that pin,
// two grid steps beyond it, instead of opening a new column.
void computeStacks(Ctx& c, const std::vector<int>& component)
{
    for (int part : component)
    {
        const auto& p = c.parts[(size_t)part];
        if (pinCount(p) != 2 || isSourceSymbol(p.symbol.id))
            continue;

        // Either pin may be the one fed by a lone vertical pin.
        int inputPin = -1, driver = -1, driverPin = -1;
        P driverDir;
        for (int pin = 0; pin < 2 && inputPin < 0; ++pin)
        {
            const auto net = p.pinNets[(size_t)pin];
            if (!isSignalNet(c, net) || terminalCount(c, net) != 2)
                continue;
            // The other part on this two-terminal net.
            int parent = -1;
            for (int q = 0; q < (int)c.parts.size() && parent < 0; ++q)
                if (q != part && std::find(c.parts[(size_t)q].pinNets.begin(), c.parts[(size_t)q].pinNets.end(), net) != c.parts[(size_t)q].pinNets.end())
                    parent = q;
            // Sources keep the textbook input layout: their first part runs
            // horizontally from the source, it does not stack on it.
            if (parent < 0 || c.role[(size_t)parent] != Role::Main || c.layer[(size_t)parent] < 0
                || isSourceSymbol(c.parts[(size_t)parent].symbol.id))
                continue;
            // Never stack onto something already riding on this part, and
            // never move a part that already carries a stack.
            if (c.stackRoot[(size_t)parent] == part || c.stackMembers.count(part) != 0)
                continue;
            const auto& d = c.parts[(size_t)parent];
            for (int dp = 0; dp < pinCount(d); ++dp)
            {
                if (d.pinNets[(size_t)dp] != net)
                    continue;
                const auto dir = pinDir(d, dp, c.rotation[(size_t)parent]);
                if (std::abs(dir.y) > 0.5f)
                {
                    inputPin = pin;
                    driver = parent;
                    driverPin = dp;
                    driverDir = dir;
                }
                break;
            }
        }
        if (inputPin < 0)
            continue;
        const auto& d = c.parts[(size_t)driver];

        int rotation = -1;
        for (int rot : { 0, 90, 180, 270 })
        {
            const auto dir = pinDir(p, inputPin, rot);
            if (std::abs(dir.x + driverDir.x) < 0.1f && std::abs(dir.y + driverDir.y) < 0.1f) { rotation = rot; break; }
        }
        if (rotation < 0)
            continue;

        const auto root = c.stackRoot[(size_t)driver] >= 0 ? c.stackRoot[(size_t)driver] : driver;
        const auto driverOffset = c.stackRoot[(size_t)driver] >= 0 ? c.stackOffset[(size_t)driver] : P();
        const auto driverPinAt = driverOffset + pinOffset(d, driverPin, c.rotation[(size_t)driver]);
        const auto inputPinAt = driverPinAt + driverDir * (c.grid * 2.0f);

        c.rotation[(size_t)part] = rotation;
        c.stackRoot[(size_t)part] = root;
        c.stackOffset[(size_t)part] = inputPinAt - pinOffset(p, inputPin, rotation);
        c.layer[(size_t)part] = c.layer[(size_t)root];
        c.stackMembers[root].push_back(part);
    }
}

// Footprint of a part together with everything stacked on it, relative to
// the part's origin.
juce::Rectangle<float> groupFootprint(const Ctx& c, int part)
{
    auto box = footprint(c.parts[(size_t)part], c.rotation[(size_t)part]);
    if (const auto found = c.stackMembers.find(part); found != c.stackMembers.end())
        for (int member : found->second)
            box = box.getUnion(footprint(c.parts[(size_t)member], c.rotation[(size_t)member])
                                   .translated(c.stackOffset[(size_t)member].x, c.stackOffset[(size_t)member].y));
    return box;
}

void placeGroupY(Ctx& c, int root, float y)
{
    c.position[(size_t)root].y = y;
    recordNetY(c, root);
    if (const auto found = c.stackMembers.find(root); found != c.stackMembers.end())
        for (int member : found->second)
        {
            c.position[(size_t)member].y = y + c.stackOffset[(size_t)member].y;
            recordNetY(c, member);
        }
}

void placeGroupX(Ctx& c, int root, float x)
{
    c.position[(size_t)root].x = x;
    c.placed[(size_t)root] = true;
    if (const auto found = c.stackMembers.find(root); found != c.stackMembers.end())
        for (int member : found->second)
        {
            c.position[(size_t)member].x = x + c.stackOffset[(size_t)member].x;
            c.placed[(size_t)member] = true;
        }
}

// Places one connected component with its left edge at x = 0 and returns
// its bounding box.
juce::Rectangle<float> placeComponent(Ctx& c, const std::vector<int>& component)
{
    std::set<int> members(component.begin(), component.end());

    // Shunts hanging from this component's nets.
    std::map<int, std::vector<int>> shuntsByNet;
    for (int i = 0; i < (int)c.parts.size(); ++i)
    {
        if (c.role[(size_t)i] != Role::Shunt)
            continue;
        for (auto net : c.parts[(size_t)i].pinNets)
            if (isSignalNet(c, net) && c.netParent[(size_t)net] >= 0 && members.count(c.netParent[(size_t)net]) != 0)
                shuntsByNet[net].push_back(i);
    }

    computeStacks(c, component);

    std::map<float, std::vector<int>> byKey;
    for (int part : component)
        if (c.stackRoot[(size_t)part] < 0)
            byKey[(float)c.layer[(size_t)part] * 2.0f].push_back(part);
    for (auto& [net, shunts] : shuntsByNet)
    {
        const auto parentLayer = (float)c.layer[(size_t)c.netParent[(size_t)net]];
        for (size_t k = 0; k < shunts.size(); ++k)
            byKey[parentLayer * 2.0f + 1.0f + (float)k * 0.01f].push_back(shunts[k]);
    }

    std::vector<Column> columns;
    for (auto& [key, parts] : byKey)
        columns.push_back({ key, parts });

    // Y: walk columns left to right, aligning each part to its input net.
    for (auto& column : columns)
    {
        std::vector<Item> items;
        for (int part : column.parts)
        {
            const auto& p = c.parts[(size_t)part];
            float desired = 0.0f;
            int bestNetParentLayer = std::numeric_limits<int>::max();
            for (int pin = 0; pin < pinCount(p); ++pin)
            {
                const auto net = p.pinNets[(size_t)pin];
                if (!isSignalNet(c, net) || !c.netHasY[(size_t)net])
                    continue;
                const auto parent = c.netParent[(size_t)net];
                const auto parentLayer = parent >= 0 ? c.layer[(size_t)parent] : 0;
                if (parentLayer < bestNetParentLayer)
                {
                    bestNetParentLayer = parentLayer;
                    desired = originForPinOnNet(c, part, pin, c.netY[(size_t)net]);
                }
            }
            const auto fp = groupFootprint(c, part);
            items.push_back({ part, desired, fp.getY(), fp.getBottom() });
        }

        const auto ys = separate(items, c.grid);
        for (size_t k = 0; k < items.size(); ++k)
            placeGroupY(c, items[k].part, snap(ys[k], c.grid));
    }

    // X: columns side by side, wider gaps where more nets cross between them.
    float x = 0.0f;
    float previousRight = 0.0f;
    for (size_t ci = 0; ci < columns.size(); ++ci)
    {
        float left = 0.0f, right = 0.0f;
        for (int part : columns[ci].parts)
        {
            const auto fp = groupFootprint(c, part);
            left = std::min(left, fp.getX());
            right = std::max(right, fp.getRight());
        }

        if (ci == 0)
        {
            x = -left;
        }
        else
        {
            std::set<int> before, after;
            for (size_t k = 0; k < columns.size(); ++k)
                for (int part : columns[k].parts)
                    for (auto net : c.parts[(size_t)part].pinNets)
                        if (isSignalNet(c, net))
                            (k < ci ? before : after).insert(net);
            int crossing = 0;
            for (auto net : before)
                if (after.count(net) != 0)
                    ++crossing;
            const auto gap = 48.0f + c.grid * (float)std::clamp(crossing - 1, 0, 6);
            x = previousRight + gap - left;
        }
        x = snap(x, c.grid);
        for (int part : columns[ci].parts)
            placeGroupX(c, part, x);
        previousRight = x + right;
    }

    juce::Rectangle<float> box;
    bool first = true;
    for (auto& column : columns)
        for (int part : column.parts)
        {
            const auto fp = groupFootprint(c, part).translated(c.position[(size_t)part].x, c.position[(size_t)part].y);
            box = first ? fp : box.getUnion(fp);
            first = false;
        }
    return box;
}

void translateParts(Ctx& c, const std::vector<int>& parts, P delta)
{
    for (int part : parts)
        c.position[(size_t)part] += delta;
    for (size_t net = 0; net < c.nets.size(); ++net)
        if (c.netHasY[net] && c.netParent[net] >= 0 && std::find(parts.begin(), parts.end(), c.netParent[net]) != parts.end())
            c.netY[net] += delta.y;
}

void placeMarkers(Ctx& c, Result& result)
{
    for (int part = 0; part < (int)c.parts.size(); ++part)
    {
        const auto& p = c.parts[(size_t)part];
        const auto rot = c.rotation[(size_t)part];
        for (int pin = 0; pin < pinCount(p); ++pin)
        {
            const auto net = p.pinNets[(size_t)pin];
            if (!isPowerNet(c, net))
                continue;

            const auto at = c.position[(size_t)part] + pinOffset(p, pin, rot);
            const auto dir = pinDir(p, pin, rot);
            const auto want = desiredPowerDirection(c, net);

            Marker marker;
            marker.part = part;
            marker.pin = pin;
            marker.netName = c.nets[(size_t)net].name;
            marker.symbolId = c.nets[(size_t)net].kind == NetKind::Ground ? "ground" : "power_port";
            marker.rotation = marker.symbolId == "power_port" && want.y > 0.0f ? 180 : 0;

            if (dir.x * want.x + dir.y * want.y > 0.5f || std::abs(dir.x) > 0.5f)
                marker.position = at + dir * c.grid;          // straight off the lead
            else
                marker.position = at + dir * c.grid + P(c.grid * 2.0f, 0.0f); // lead points away: step aside
            marker.position = { snap(marker.position.x, c.grid), snap(marker.position.y, c.grid) };
            result.markers.push_back(marker);
        }
    }

    // Instrument inputs: a label beside the instrument pin and a matching
    // label on the probed net, just past the pin that drives that net.
    std::map<int, int> labelsOnNet;
    for (int part = 0; part < (int)c.parts.size(); ++part)
    {
        if (c.role[(size_t)part] != Role::Instrument)
            continue;
        const auto& p = c.parts[(size_t)part];
        const auto rot = c.rotation[(size_t)part];
        for (int pin = 0; pin < pinCount(p); ++pin)
        {
            const auto net = p.pinNets[(size_t)pin];
            if (!isSignalNet(c, net))
                continue;
            const auto name = p.refdes + "." + p.symbol.pins[(size_t)pin].name;

            Marker atInstrument;
            atInstrument.part = part;
            atInstrument.pin = pin;
            atInstrument.symbolId = "net_label";
            atInstrument.netName = name;
            const auto at = c.position[(size_t)part] + pinOffset(p, pin, rot);
            // Far enough out that the label text clears the instrument body.
            atInstrument.position = at + pinDir(p, pin, rot) * (c.grid * 5.0f);
            atInstrument.position = { snap(atInstrument.position.x, c.grid), snap(atInstrument.position.y, c.grid) };
            result.markers.push_back(atInstrument);

            const auto driver = c.netParent[(size_t)net];
            if (driver < 0 || !c.placed[(size_t)driver])
                continue;
            const auto& d = c.parts[(size_t)driver];
            int driverPin = -1;
            for (int dp = 0; dp < pinCount(d); ++dp)
                if (d.pinNets[(size_t)dp] == net) { driverPin = dp; break; }
            if (driverPin < 0)
                continue;
            const auto dAt = c.position[(size_t)driver] + pinOffset(d, driverPin, c.rotation[(size_t)driver]);
            const auto dDir = pinDir(d, driverPin, c.rotation[(size_t)driver]);
            const auto lineY = std::abs(dDir.y) > 0.5f ? dAt.y + dDir.y * c.grid : dAt.y;
            const auto step = (float)labelsOnNet[net]++ * c.grid * 2.0f;
            const auto x = std::abs(dDir.x) > 0.5f ? dAt.x + dDir.x * (c.grid * 2.0f + step) : dAt.x + c.grid * 2.0f + step;

            Marker onNet;
            onNet.symbolId = "net_label";
            onNet.netName = name;
            onNet.position = { snap(x, c.grid), snap(lineY - c.grid * 2.0f, c.grid) };
            onNet.onNet = true;
            onNet.net = net;
            result.markers.push_back(onNet);
        }
    }
}

}

Result layoutSchematic(const std::vector<Part>& parts, const std::vector<Net>& nets, float gridSize)
{
    Ctx c { parts, nets, gridSize, {}, {}, {}, {}, {}, {}, {}, {} };
    const auto n = parts.size();
    c.position.assign(n, {});
    c.placed.assign(n, false);
    c.netY.assign(nets.size(), 0.0f);
    c.netHasY.assign(nets.size(), false);
    c.stackRoot.assign(n, -1);
    c.stackOffset.assign(n, {});

    classify(c);
    const auto components = layerComponents(c);
    orient(c);

    // Components stacked top to bottom, each starting at the left margin.
    constexpr float margin = 96.0f;
    float nextTop = margin;
    float rightmost = margin;
    for (const auto& component : components)
    {
        std::vector<int> members = component;
        for (int i = 0; i < (int)n; ++i)
            if (c.role[(size_t)i] == Role::Shunt)
                for (auto net : parts[(size_t)i].pinNets)
                    if (isSignalNet(c, net) && c.netParent[(size_t)net] >= 0
                        && std::find(component.begin(), component.end(), c.netParent[(size_t)net]) != component.end()
                        && std::find(members.begin(), members.end(), i) == members.end())
                        members.push_back(i);

        const auto box = placeComponent(c, component);
        const P delta { snap(margin - box.getX(), gridSize), snap(nextTop - box.getY(), gridSize) };
        translateParts(c, members, delta);
        nextTop = box.getBottom() + delta.y + 120.0f;
        rightmost = std::max(rightmost, box.getRight() + delta.x);
    }

    // Supply block under the circuit, sources side by side.
    float supplyX = margin;
    bool anySupply = false;
    for (int i = 0; i < (int)n; ++i)
    {
        if (c.role[(size_t)i] != Role::Supply)
            continue;
        anySupply = true;
        const auto fp = footprint(parts[(size_t)i], c.rotation[(size_t)i]);
        c.position[(size_t)i] = { snap(supplyX - fp.getX(), gridSize), snap(nextTop + 48.0f - fp.getY(), gridSize) };
        c.placed[(size_t)i] = true;
        supplyX += fp.getWidth() + 120.0f;
    }
    juce::ignoreUnused(anySupply);

    // Unplaced leftovers (parts on no signal net at all) go beside the supplies.
    for (int i = 0; i < (int)n; ++i)
    {
        if (c.placed[(size_t)i] || c.role[(size_t)i] == Role::Instrument)
            continue;
        const auto fp = footprint(parts[(size_t)i], c.rotation[(size_t)i]);
        c.position[(size_t)i] = { snap(supplyX - fp.getX(), gridSize), snap(nextTop + 48.0f - fp.getY(), gridSize) };
        c.placed[(size_t)i] = true;
        supplyX += fp.getWidth() + 120.0f;
    }

    // Instruments in a column on the right, each beside the net it probes.
    std::vector<Item> instruments;
    for (int i = 0; i < (int)n; ++i)
    {
        if (c.role[(size_t)i] != Role::Instrument)
            continue;
        float desired = margin;
        for (int pin = 0; pin < pinCount(parts[(size_t)i]); ++pin)
        {
            const auto net = parts[(size_t)i].pinNets[(size_t)pin];
            if (isSignalNet(c, net) && c.netHasY[(size_t)net])
            {
                desired = originForPinOnNet(c, i, pin, c.netY[(size_t)net]);
                break;
            }
        }
        const auto fp = footprint(parts[(size_t)i], 0);
        instruments.push_back({ i, desired, fp.getY(), fp.getBottom() });
    }
    if (!instruments.empty())
    {
        const auto ys = separate(instruments, gridSize * 2.0f);
        float left = 0.0f;
        for (const auto& item : instruments)
            left = std::min(left, footprint(parts[(size_t)item.part], 0).getX());
        const auto x = snap(rightmost + 144.0f - left, gridSize);
        for (size_t k = 0; k < instruments.size(); ++k)
        {
            c.position[(size_t)instruments[k].part] = { x, snap(ys[k], gridSize) };
            c.placed[(size_t)instruments[k].part] = true;
        }
    }

    Result result;
    result.positions = c.position;
    result.rotations = c.rotation;
    placeMarkers(c, result);
    return result;
}
}
