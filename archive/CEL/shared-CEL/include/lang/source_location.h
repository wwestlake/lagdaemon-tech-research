#pragma once

#include <string>

namespace ce::lang {

// 1-based line/column, matching how every editor and compiler diagnostic
// convention displays positions to a human.
struct SourceLocation {
    int line = 1;
    int column = 1;

    std::string ToString() const;
};

} // namespace ce::lang
