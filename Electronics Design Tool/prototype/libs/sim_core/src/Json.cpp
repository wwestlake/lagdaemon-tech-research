#include "sim_core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sim
{
void Json::set(const std::string& key, Json v)
{
    for (auto& m : members_)
        if (m.first == key)
        {
            m.second = std::move(v);
            return;
        }
    members_.emplace_back(key, std::move(v));
}

const Json* Json::find(const std::string& key) const
{
    for (const auto& m : members_)
        if (m.first == key)
            return &m.second;
    return nullptr;
}

namespace
{
void quote(std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                }
                else
                    out += (char)c;
        }
    }
    out += '"';
}
}

void Json::dumpTo(std::string& out, int indent, int depth) const
{
    auto newline = [&](int d) {
        if (indent <= 0) return;
        out += '\n';
        out.append((std::size_t)(indent * d), ' ');
    };
    switch (type_)
    {
        case Type::Null: out += "null"; break;
        case Type::Boolean: out += integer_ ? "true" : "false"; break;
        case Type::Integer: out += std::to_string(integer_); break;
        case Type::Number:
        {
            if (!std::isfinite(number_)) { out += "null"; break; }
            char buffer[40];
            std::snprintf(buffer, sizeof buffer, "%.17g", number_);
            out += buffer;
            break;
        }
        case Type::String: quote(out, string_); break;
        case Type::Array:
            out += '[';
            for (std::size_t i = 0; i < items_.size(); ++i)
            {
                if (i) out += ',';
                newline(depth + 1);
                items_[i].dumpTo(out, indent, depth + 1);
            }
            if (!items_.empty()) newline(depth);
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (std::size_t i = 0; i < members_.size(); ++i)
            {
                if (i) out += ',';
                newline(depth + 1);
                quote(out, members_[i].first);
                out += indent > 0 ? ": " : ":";
                members_[i].second.dumpTo(out, indent, depth + 1);
            }
            if (!members_.empty()) newline(depth);
            out += '}';
            break;
    }
}

std::string Json::dump(int indent) const
{
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

namespace
{
struct Parser
{
    const std::string& s;
    std::size_t i = 0;
    std::string error;

    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool fail(const std::string& what)
    {
        if (error.empty()) error = what + " at character " + std::to_string(i) + ".";
        return false;
    }
    bool literal(const char* word)
    {
        std::size_t n = std::char_traits<char>::length(word);
        if (s.compare(i, n, word) != 0) return false;
        i += n;
        return true;
    }
    bool parseString(std::string& out)
    {
        if (i >= s.size() || s[i] != '"') return fail("Expected a string");
        ++i;
        while (i < s.size() && s[i] != '"')
        {
            char c = s[i++];
            if (c == '\\')
            {
                if (i >= s.size()) return fail("Unfinished escape");
                char e = s[i++];
                switch (e)
                {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        if (i + 4 > s.size()) return fail("Short \\u escape");
                        const unsigned code = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (code < 0x80) out += (char)code;
                        else if (code < 0x800) { out += (char)(0xC0 | (code >> 6)); out += (char)(0x80 | (code & 0x3F)); }
                        else { out += (char)(0xE0 | (code >> 12)); out += (char)(0x80 | ((code >> 6) & 0x3F)); out += (char)(0x80 | (code & 0x3F)); }
                        break;
                    }
                    default: return fail("Unknown escape");
                }
            }
            else
                out += c;
        }
        if (i >= s.size()) return fail("Unfinished string");
        ++i;
        return true;
    }
    bool parseValue(Json& out)
    {
        skip();
        if (i >= s.size()) return fail("Unexpected end");
        const char c = s[i];
        if (c == '{')
        {
            ++i;
            out = Json::object();
            skip();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;)
            {
                skip();
                std::string key;
                if (!parseString(key)) return false;
                skip();
                if (i >= s.size() || s[i] != ':') return fail("Expected ':'");
                ++i;
                Json v;
                if (!parseValue(v)) return false;
                out.set(key, std::move(v));
                skip();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("Expected ',' or '}'");
            }
        }
        if (c == '[')
        {
            ++i;
            out = Json::array();
            skip();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;)
            {
                Json v;
                if (!parseValue(v)) return false;
                out.push(std::move(v));
                skip();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("Expected ',' or ']'");
            }
        }
        if (c == '"')
        {
            std::string text;
            if (!parseString(text)) return false;
            out = Json::string(std::move(text));
            return true;
        }
        if (literal("true")) { out = Json::boolean(true); return true; }
        if (literal("false")) { out = Json::boolean(false); return true; }
        if (literal("null")) { out = Json(); return true; }
        // Number
        const std::size_t start = i;
        if (s[i] == '-' || s[i] == '+') ++i;
        bool fraction = false;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '-' || s[i] == '+'))
        {
            if (s[i] == '.' || s[i] == 'e' || s[i] == 'E') fraction = true;
            ++i;
        }
        if (i == start) return fail("Unexpected character");
        const std::string text = s.substr(start, i - start);
        if (fraction)
            out = Json::number(std::strtod(text.c_str(), nullptr));
        else
            out = Json::integer(std::strtoll(text.c_str(), nullptr, 10));
        return true;
    }
};
}

bool Json::parse(const std::string& text, Json& out, std::string& error)
{
    Parser p { text };
    Json value;
    if (!p.parseValue(value))
    {
        error = p.error;
        return false;
    }
    p.skip();
    if (p.i != text.size())
    {
        error = "Unexpected text after the JSON value at character " + std::to_string(p.i) + ".";
        return false;
    }
    out = std::move(value);
    return true;
}
}
