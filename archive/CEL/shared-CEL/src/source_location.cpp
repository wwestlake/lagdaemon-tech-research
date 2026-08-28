#include "lang/source_location.h"

namespace ce::lang {

std::string SourceLocation::ToString() const {
    return std::to_string(line) + ":" + std::to_string(column);
}

} // namespace ce::lang
