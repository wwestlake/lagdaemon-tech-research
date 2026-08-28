#include "lang/type.h"

namespace ce::lang {

const char* ToString(Type type) {
    switch (type) {
        case Type::Void: return "void";
        case Type::Int: return "int";
        case Type::Float: return "float";
        case Type::Bool: return "bool";
        case Type::Vec2: return "vec2";
        case Type::Vec3: return "vec3";
        case Type::Vec4: return "vec4";
        case Type::Mat2: return "mat2";
        case Type::Mat3: return "mat3";
        case Type::Mat4: return "mat4";
        case Type::Entity: return "entity";
        case Type::String: return "string";
        case Type::Unknown: return "<error>";
    }
    return "<error>";
}

Type ParseTypeName(const std::string& name) {
    if (name == "int") return Type::Int;
    if (name == "float") return Type::Float;
    if (name == "bool") return Type::Bool;
    if (name == "vec2") return Type::Vec2;
    if (name == "vec3") return Type::Vec3;
    if (name == "vec4") return Type::Vec4;
    if (name == "mat2") return Type::Mat2;
    if (name == "mat3") return Type::Mat3;
    if (name == "mat4") return Type::Mat4;
    if (name == "entity") return Type::Entity;
    return Type::Unknown;
}

const char* ToString(IntrinsicDomain domain) {
    switch (domain) {
        case IntrinsicDomain::Core: return "core";
        case IntrinsicDomain::World: return "world";
    }
    return "<unknown domain>";
}

IntrinsicDomainSet IntrinsicDomainSet::All() {
    IntrinsicDomainSet set;
    set.mask_ = (1u << static_cast<unsigned>(IntrinsicDomain::Core)) | (1u << static_cast<unsigned>(IntrinsicDomain::World));
    return set;
}

IntrinsicDomainSet IntrinsicDomainSet::Only(const std::vector<IntrinsicDomain>& domains) {
    IntrinsicDomainSet set;
    for (IntrinsicDomain domain : domains) {
        set.mask_ |= (1u << static_cast<unsigned>(domain));
    }
    return set;
}

bool IntrinsicDomainSet::Contains(IntrinsicDomain domain) const {
    return (mask_ & (1u << static_cast<unsigned>(domain))) != 0;
}

} // namespace ce::lang
