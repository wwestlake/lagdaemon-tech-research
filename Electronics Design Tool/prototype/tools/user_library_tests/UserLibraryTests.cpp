#include "UserBlockLibrary.h"
#include <iostream>

int main()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("DEWB-library-tests", {}, true);
    user_blocks::Library library(folder);
    int failures = 0, checks = 0, changes = 0;
    const auto check = [&](bool condition, const char* message) {
        ++checks;
        if (!condition) { ++failures; std::cerr << "FAIL: " << message << "\n"; }
    };
    library.onChanged = [&] { ++changes; };
    auto definition = juce::JSON::parse(R"({"schema":"djehuti_electronics_user_block","version":1,"name":"Signal Pass-Through","category":"","description":"Input to output","sourceBlock":"A1","sourceSheet":"S1","circuit":{"components":[{"id":"A1","symbol":"sub_block","childSheet":"S1","ports":[{"id":"pin-1","name":"Input"},{"id":"pin-2","name":"Output"}]},{"id":"P1","symbol":"block_port","sheet":"S1","params":{"portId":"pin-1"}},{"id":"P2","symbol":"block_port","sheet":"S1","params":{"portId":"pin-2"}}],"wires":[{"a":"P1.PORT","b":"P2.PORT"}]}})");
    juce::File saved;
    juce::String error;
    check(library.save(definition, saved, error), "save pass-through");
    check(changes == 1 && library.entries().size() == 1, "immediate shared collection update");
    check(library.entries()[0].category.isEmpty(), "uncategorized at root");
    check(juce::JSON::toString(juce::JSON::parse(saved)) == juce::JSON::toString(definition), "all ports, identities, internal wires and properties preserved");
    check(library.save(definition, saved, error) && library.entries().size() == 1, "repeat save has no duplicate");
    auto collision = definition.clone();
    collision.getDynamicObject()->setProperty("name", "Signal Pass Through");
    juce::File other;
    check(library.save(collision, other, error) && other != saved && library.entries().size() == 2, "filename sanitization cannot overwrite different component");
    check(library.manage("create_category", "Signals", {}, error), "create empty category");
    check(user_blocks::Library(folder).categories().contains("Signals"), "empty category survives reopen");
    check(!library.manage("create_category", "signals", {}, error), "case insensitive category collision");
    check(library.manage("move_component", "Signal Pass-Through", "Signals", error), "move component");
    check(!library.manage("delete_category", "Signals", {}, error), "reject deleting nonempty category");
    check(library.manage("rename_category", "Signals", "Controls", error), "rename populated category");
    check(juce::JSON::parse(user_blocks::Library(folder).find("Signal Pass-Through")).getProperty("category", {}).toString() == "Controls", "category rename persisted");
    check(library.manage("rename_component", "Signal Pass-Through", "Renamed", error), "rename template");
    check(!library.find("Signal Pass-Through").existsAsFile() && library.find("Renamed").existsAsFile(), "name lookup follows metadata rather than old filename");
    check(!library.manage("rename_component", "Renamed", "Signal Pass Through", error), "reject duplicate template name");
    check(library.manage("move_component", "Renamed", {}, error), "move to root");
    check(library.manage("delete_category", "Controls", {}, error), "delete empty category");
    check(library.manage("delete_component", "Renamed", {}, error), "delete template");
    check(!library.find("Renamed").existsAsFile() && library.entries().size() == 1, "discovery reflects delete");
    check(!library.manage("move_component", "Signal Pass Through", "Missing", error), "reject missing category");
    check(!library.save(juce::var(), saved, error), "invalid definition rejected");
    folder.getChildFile("broken.block.json").replaceWithText("{broken");
    check(library.entries().size() == 1, "corrupt definition ignored");
    const auto blocker = folder.getChildFile("not-a-directory");
    blocker.replaceWithText("occupied");
    user_blocks::Library unwritable(blocker);
    check(!unwritable.save(definition, saved, error) && error.isNotEmpty(), "write failure returns meaningful error");
    folder.deleteRecursively(); // unique test-owned directory only
    std::cout << checks - failures << "/" << checks << " user library checks passed\n";
    return failures == 0 ? 0 : 1;
}
