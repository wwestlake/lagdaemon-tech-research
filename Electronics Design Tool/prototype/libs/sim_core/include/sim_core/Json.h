#pragma once

// A small JSON value for the recording manifest (sim_core has no JUCE).
// Objects keep their key order, so writing is deterministic and unknown
// fields read from a newer manifest are preserved when it is written back.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sim
{
class Json
{
public:
    enum class Type { Null, Boolean, Integer, Number, String, Array, Object };

    Json() = default;
    static Json boolean(bool b) { Json j; j.type_ = Type::Boolean; j.integer_ = b ? 1 : 0; return j; }
    static Json integer(std::int64_t v) { Json j; j.type_ = Type::Integer; j.integer_ = v; return j; }
    static Json number(double v) { Json j; j.type_ = Type::Number; j.number_ = v; return j; }
    static Json string(std::string s) { Json j; j.type_ = Type::String; j.string_ = std::move(s); return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }

    bool asBool(bool fallback = false) const { return type_ == Type::Boolean ? integer_ != 0 : fallback; }
    std::int64_t asInteger(std::int64_t fallback = 0) const
    {
        return type_ == Type::Integer ? integer_ : type_ == Type::Number ? (std::int64_t)number_ : fallback;
    }
    double asNumber(double fallback = 0.0) const
    {
        return type_ == Type::Number ? number_ : type_ == Type::Integer ? (double)integer_ : fallback;
    }
    const std::string& asString() const { return string_; }

    // Arrays
    void push(Json v) { items_.push_back(std::move(v)); }
    const std::vector<Json>& items() const { return items_; }

    // Objects: set replaces an existing key in place (order kept).
    void set(const std::string& key, Json v);
    const Json* find(const std::string& key) const;
    const std::vector<std::pair<std::string, Json>>& members() const { return members_; }

    std::string dump(int indent = 2) const;
    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    std::int64_t integer_ = 0;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;
};
}
