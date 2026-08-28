#include "lang/sema.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ce::lang {

namespace {

struct FunctionSignature {
    std::vector<Type> paramTypes;
    Type returnType = Type::Void;
};

const char* BinaryOpSpelling(BinaryOp op) {
    switch (op) {
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::Eq: return "==";
        case BinaryOp::Neq: return "!=";
        case BinaryOp::Lt: return "<";
        case BinaryOp::Gt: return ">";
        case BinaryOp::Le: return "<=";
        case BinaryOp::Ge: return ">=";
        case BinaryOp::And: return "&&";
        case BinaryOp::Or: return "||";
    }
    return "?";
}

// Vec2/Vec3/Vec4 share identical operator eligibility (componentwise
// +/-, scalar */÷, equality) -- this collects them so CheckBinary/
// CheckUnary don't repeat a three-way OR at every call site. Deliberately
// NOT "any aggregate type" -- if a non-vector aggregate is ever added,
// it should get its own explicit eligibility, not silently inherit this.
bool IsVecType(Type t) {
    return t == Type::Vec2 || t == Type::Vec3 || t == Type::Vec4;
}

// Component count for a vector type -- the one piece of size-specific
// knowledge sema needs (to validate a member-access field letter);
// codegen has its own copy of this same mapping (module_builder.cpp)
// since the two are compiled from different translation units with no
// shared header for it -- see VecComponentCount there.
int VecComponentCount(Type t) {
    switch (t) {
        case Type::Vec2: return 2;
        case Type::Vec3: return 3;
        case Type::Vec4: return 4;
        default: return 0;
    }
}

// Mat2/Mat3/Mat4 -- column-major (matching the existing convention
// juce::Matrix3D<float>/cgltf_node_transform_local already use
// elsewhere in Creation Engine's own code, see AnimationSampler.cpp's
// ComposeTRS). Matrices deliberately don't participate in IsVecType --
// member access (.x/.y/...) has no meaning on a matrix, only CheckBinary
// needs to know about them.
bool IsMatType(Type t) {
    return t == Type::Mat2 || t == Type::Mat3 || t == Type::Mat4;
}

int MatDimension(Type t) {
    switch (t) {
        case Type::Mat2: return 2;
        case Type::Mat3: return 3;
        case Type::Mat4: return 4;
        default: return 0;
    }
}

// The vecN type with the same component count as matN's dimension --
// what matN * vecN (matrix-vector product) returns, and what a matN's
// columns are made of.
Type MatVecType(Type mat) {
    switch (mat) {
        case Type::Mat2: return Type::Vec2;
        case Type::Mat3: return Type::Vec3;
        case Type::Mat4: return Type::Vec4;
        default: return Type::Unknown;
    }
}

const char* AssignOpSpelling(AssignOp op) {
    switch (op) {
        case AssignOp::Assign: return "=";
        case AssignOp::AddAssign: return "+=";
        case AssignOp::SubAssign: return "-=";
        case AssignOp::MulAssign: return "*=";
        case AssignOp::DivAssign: return "/=";
    }
    return "?";
}

BinaryOp CompoundAssignToBinary(AssignOp op) {
    switch (op) {
        case AssignOp::AddAssign: return BinaryOp::Add;
        case AssignOp::SubAssign: return BinaryOp::Sub;
        case AssignOp::MulAssign: return BinaryOp::Mul;
        case AssignOp::DivAssign: return BinaryOp::Div;
        case AssignOp::Assign: break;
    }
    return BinaryOp::Add; // unreachable: callers never route AssignOp::Assign through this.
}

// Class kept internal to this translation unit -- sema.h only exposes
// the single AnalyzeProgram entry point, matching compiler.h/Parser's
// own "public function, private machinery" shape.
class Sema {
public:
    explicit Sema(DiagnosticEngine& diagnostics, IntrinsicDomainSet allowedDomains)
        : diagnostics_(diagnostics), allowedDomains_(allowedDomains) {
        SeedIntrinsics();
    }

    bool Analyze(Program& program) {
        // Pass 1: function signatures (purely syntactic -- type NAMES,
        // not expressions) and full global var checking. Globals don't
        // forward-reference each other in v1 (each is checked, in
        // textual order, using only what's already registered), which
        // keeps this a single straightforward pass instead of needing a
        // dependency-ordered one.
        for (Decl* decl : program.decls) {
            if (decl->kind == DeclKind::Func) {
                RegisterFuncSignature(*decl->funcDecl);
            } else {
                CheckGlobalVarDecl(*decl->varDecl);
            }
        }
        // Pass 2: function bodies. All signatures and globals are known
        // by now, so call/reference order across functions doesn't matter.
        for (Decl* decl : program.decls) {
            if (decl->kind == DeclKind::Func) {
                CheckFuncBody(*decl->funcDecl);
            }
        }
        return !diagnostics_.HasErrors();
    }

private:
    void Report(DiagCode code, SourceLocation loc, std::string message) {
        diagnostics_.Report(code, DiagnosticSeverity::Error, loc, std::move(message));
    }

    void SeedIntrinsics() {
// Sema only cares about name/return/param types plus (GS-Interop) domain
// -- cSymbol and purity are GS5's IR-gen/JIT-registration concerns
// (module_builder.cpp), deliberately ignored here rather than
// duplicating a second table. Every intrinsic is seeded into
// functions_/intrinsicNames_ regardless of allowedDomains_ -- a user
// function still can't shadow a built-in name even in a host that
// blocks its domain (see CheckCall's separate, later domain check for
// where a blocked CALL is actually rejected).
#define CEL_INTRINSIC0(name, cSymbol, purity, domain, ret) \
    functions_[#name] = FunctionSignature{ {}, Type::ret };  \
    intrinsicDomains_[#name] = IntrinsicDomain::domain;
#define CEL_INTRINSIC1(name, cSymbol, purity, domain, ret, p1) \
    functions_[#name] = FunctionSignature{ { Type::p1 }, Type::ret }; \
    intrinsicDomains_[#name] = IntrinsicDomain::domain;
#define CEL_INTRINSIC2(name, cSymbol, purity, domain, ret, p1, p2) \
    functions_[#name] = FunctionSignature{ { Type::p1, Type::p2 }, Type::ret }; \
    intrinsicDomains_[#name] = IntrinsicDomain::domain;
#define CEL_INTRINSIC3(name, cSymbol, purity, domain, ret, p1, p2, p3) \
    functions_[#name] = FunctionSignature{ { Type::p1, Type::p2, Type::p3 }, Type::ret }; \
    intrinsicDomains_[#name] = IntrinsicDomain::domain;
#define CEL_INTRINSIC4(name, cSymbol, purity, domain, ret, p1, p2, p3, p4) \
    functions_[#name] = FunctionSignature{ { Type::p1, Type::p2, Type::p3, Type::p4 }, Type::ret }; \
    intrinsicDomains_[#name] = IntrinsicDomain::domain;
#include "lang/intrinsics.def"
#undef CEL_INTRINSIC0
#undef CEL_INTRINSIC1
#undef CEL_INTRINSIC2
#undef CEL_INTRINSIC3
#undef CEL_INTRINSIC4
        for (const auto& entry : functions_) {
            intrinsicNames_.insert(entry.first);
        }
    }

    void RegisterFuncSignature(FuncDecl& f) {
        if (functions_.find(f.name) != functions_.end()) {
            const bool isIntrinsic = intrinsicNames_.count(f.name) != 0;
            Report(DiagCode::DuplicateFunction, f.loc,
                   "function '" + f.name + "' " + (isIntrinsic ? "shadows a built-in intrinsic" : "is already defined"));
            return;
        }

        FunctionSignature sig;
        sig.returnType = f.returnType.empty() ? Type::Void : ParseTypeName(f.returnType);
        if (!f.returnType.empty() && sig.returnType == Type::Unknown) {
            Report(DiagCode::UnknownType, f.loc, "unknown return type '" + f.returnType + "'");
        }

        std::unordered_set<std::string> paramNames;
        for (const Param& p : f.params) {
            const Type pt = ParseTypeName(p.type);
            if (pt == Type::Unknown) {
                Report(DiagCode::UnknownType, p.loc, "unknown parameter type '" + p.type + "'");
            }
            if (!paramNames.insert(p.name).second) {
                Report(DiagCode::DuplicateParam, p.loc, "duplicate parameter '" + p.name + "'");
            }
            sig.paramTypes.push_back(pt);
        }
        functions_[f.name] = sig;
    }

    void CheckGlobalVarDecl(GlobalVarDecl& v) {
        if (globals_.find(v.name) != globals_.end() || functions_.find(v.name) != functions_.end()) {
            Report(DiagCode::DuplicateGlobal, v.loc, "global '" + v.name + "' is already defined");
            return;
        }

        const Type initType = CheckExpr(*v.initExpr);
        Type declared = initType;
        if (!v.declaredType.empty()) {
            declared = ParseTypeName(v.declaredType);
            if (declared == Type::Unknown) {
                Report(DiagCode::UnknownType, v.loc, "unknown type '" + v.declaredType + "'");
                declared = initType;
            } else if (initType != Type::Unknown && declared != initType) {
                Report(DiagCode::TypeMismatch, v.loc,
                       "cannot initialize '" + v.name + "' of type " + ToString(declared) + " with " +
                           ToString(initType));
            }
        }
        v.resolvedType = declared;
        globals_[v.name] = declared;
    }

    void CheckFuncBody(FuncDecl& f) {
        PushScope();
        // Duplicate param names were already reported in
        // RegisterFuncSignature -- insert directly rather than going
        // through DeclareLocal's own duplicate check, which would
        // double-report the same problem.
        for (const Param& p : f.params) {
            scopes_.back()[p.name] = ParseTypeName(p.type);
        }

        int loopDepth = 0;
        const bool returnsOnAllPaths = CheckStmt(*f.body, f, loopDepth);
        const Type funcReturnType = f.returnType.empty() ? Type::Void : ParseTypeName(f.returnType);
        if (funcReturnType != Type::Void && !returnsOnAllPaths) {
            Report(DiagCode::MissingReturnOnAllPaths, f.loc,
                   "function '" + f.name + "' does not return a value on all paths");
        }

        PopScope();
    }

    // --- Scopes ---------------------------------------------------------

    void PushScope() { scopes_.emplace_back(); }
    void PopScope() { scopes_.pop_back(); }

    void DeclareLocal(const std::string& name, Type type, SourceLocation loc) {
        auto& scope = scopes_.back();
        if (scope.find(name) != scope.end()) {
            Report(DiagCode::DuplicateLocal, loc, "'" + name + "' is already declared in this scope");
            return;
        }
        scope[name] = type;
    }

    Type LookupIdentifier(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            const auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        const auto globalIt = globals_.find(name);
        if (globalIt != globals_.end()) {
            return globalIt->second;
        }
        return Type::Unknown;
    }

    // --- Statements -------------------------------------------------------
    // Returns true iff this statement definitely returns on every path
    // through it -- the return-on-all-paths analysis. Loops are always
    // conservative (false): a while/for might execute zero times, so a
    // return inside one is never guaranteed to run.

    bool CheckStmt(Stmt& s, FuncDecl& currentFunc, int& loopDepth) {
        switch (s.kind) {
            case StmtKind::Block: {
                PushScope();
                bool returns = false;
                for (Stmt* child : s.statements) {
                    returns = CheckStmt(*child, currentFunc, loopDepth) || returns;
                }
                PopScope();
                return returns;
            }
            case StmtKind::VarDecl: {
                const Type initType = CheckExpr(*s.initExpr);
                Type declared = initType;
                if (!s.declaredType.empty()) {
                    declared = ParseTypeName(s.declaredType);
                    if (declared == Type::Unknown) {
                        Report(DiagCode::UnknownType, s.loc, "unknown type '" + s.declaredType + "'");
                        declared = initType;
                    } else if (initType != Type::Unknown && declared != initType) {
                        Report(DiagCode::TypeMismatch, s.loc,
                               "cannot initialize '" + s.name + "' of type " + ToString(declared) + " with " +
                                   ToString(initType));
                    }
                }
                s.resolvedType = declared;
                DeclareLocal(s.name, declared, s.loc);
                return false;
            }
            case StmtKind::Assign: {
                const Type targetType = CheckExpr(*s.assignTarget);
                if (!IsValidLvalue(*s.assignTarget)) {
                    Report(DiagCode::InvalidLvalue, s.assignTarget->loc,
                           "assignment target must be a variable or a vec3 field");
                }
                const Type valueType = CheckExpr(*s.assignValue);
                if (targetType != Type::Unknown && valueType != Type::Unknown) {
                    if (s.assignOp == AssignOp::Assign) {
                        if (targetType != valueType) {
                            Report(DiagCode::TypeMismatch, s.loc,
                                   "cannot assign " + std::string(ToString(valueType)) + " to " +
                                       ToString(targetType));
                        }
                    } else if (!IsValidArithmeticCombo(targetType, valueType, CompoundAssignToBinary(s.assignOp))) {
                        Report(DiagCode::TypeMismatch, s.loc,
                               std::string("operator '") + AssignOpSpelling(s.assignOp) + "' is not defined for " +
                                   ToString(targetType) + " and " + ToString(valueType));
                    }
                }
                return false;
            }
            case StmtKind::If: {
                CheckCondition(*s.condition, "if");
                const bool thenReturns = CheckStmt(*s.thenBranch, currentFunc, loopDepth);
                const bool elseReturns = s.elseBranch != nullptr && CheckStmt(*s.elseBranch, currentFunc, loopDepth);
                return thenReturns && s.elseBranch != nullptr && elseReturns;
            }
            case StmtKind::While: {
                CheckCondition(*s.condition, "while");
                ++loopDepth;
                CheckStmt(*s.body, currentFunc, loopDepth);
                --loopDepth;
                return false;
            }
            case StmtKind::For: {
                PushScope(); // the loop variable is scoped to the loop, not the enclosing block.
                if (s.forInit != nullptr) {
                    CheckStmt(*s.forInit, currentFunc, loopDepth);
                }
                CheckCondition(*s.forCond, "for");
                if (s.forStep != nullptr) {
                    CheckStmt(*s.forStep, currentFunc, loopDepth);
                }
                ++loopDepth;
                CheckStmt(*s.body, currentFunc, loopDepth);
                --loopDepth;
                PopScope();
                return false;
            }
            case StmtKind::Break:
                if (loopDepth == 0) {
                    Report(DiagCode::BreakOutsideLoop, s.loc, "'break' outside a loop");
                }
                return false;
            case StmtKind::Continue:
                if (loopDepth == 0) {
                    Report(DiagCode::ContinueOutsideLoop, s.loc, "'continue' outside a loop");
                }
                return false;
            case StmtKind::Return: {
                const Type funcReturnType =
                    currentFunc.returnType.empty() ? Type::Void : ParseTypeName(currentFunc.returnType);
                if (s.returnValue == nullptr) {
                    if (funcReturnType != Type::Void) {
                        Report(DiagCode::MissingReturnValue, s.loc,
                               "function '" + currentFunc.name + "' must return a value of type " +
                                   ToString(funcReturnType));
                    }
                } else {
                    const Type valueType = CheckExpr(*s.returnValue);
                    if (funcReturnType == Type::Void) {
                        Report(DiagCode::ReturnValueInVoidFunction, s.loc,
                               "function '" + currentFunc.name + "' returns void and cannot return a value");
                    } else if (valueType != Type::Unknown && valueType != funcReturnType) {
                        Report(DiagCode::ReturnTypeMismatch, s.loc,
                               "cannot return " + std::string(ToString(valueType)) +
                                   " from a function declared to return " + ToString(funcReturnType));
                    }
                }
                return true;
            }
            case StmtKind::ExprStmt:
                CheckExpr(*s.expr);
                return false;
        }
        return false;
    }

    void CheckCondition(Expr& cond, const char* context) {
        const Type condType = CheckExpr(cond);
        if (condType != Type::Unknown && condType != Type::Bool) {
            Report(DiagCode::NonBoolCondition, cond.loc,
                   std::string(context) + " condition must be bool, got " + ToString(condType));
        }
    }

    static bool IsValidLvalue(const Expr& e) {
        if (e.kind == ExprKind::Identifier) {
            return true;
        }
        return e.kind == ExprKind::Member && e.lhs->kind == ExprKind::Identifier;
    }

    static bool IsValidArithmeticCombo(Type target, Type value, BinaryOp op) {
        switch (op) {
            case BinaryOp::Add:
            case BinaryOp::Sub:
                return (target == Type::Int && value == Type::Int) || (target == Type::Float && value == Type::Float) ||
                       (target == Type::Vec3 && value == Type::Vec3);
            case BinaryOp::Mul:
            case BinaryOp::Div:
                return (target == Type::Int && value == Type::Int) || (target == Type::Float && value == Type::Float) ||
                       (target == Type::Vec3 && value == Type::Float);
            default:
                return false;
        }
    }

    // --- Expressions --------------------------------------------------
    // Every case sets e.type before returning it, including Type::Unknown
    // on failure -- codegen (GS4) can rely on `type` always being set on
    // any Expr it walks after a successful Analyze().

    Type CheckExpr(Expr& e) {
        if (e.kind == ExprKind::Binary || e.kind == ExprKind::Unary) {
            FoldConstants(e); // may rewrite e.kind to a literal in place; re-dispatch on whatever it ends up as.
        }

        switch (e.kind) {
            case ExprKind::IntLiteral:
                e.type = Type::Int;
                return e.type;
            case ExprKind::FloatLiteral:
                e.type = Type::Float;
                return e.type;
            case ExprKind::BoolLiteral:
                e.type = Type::Bool;
                return e.type;
            case ExprKind::StringLiteral:
                e.type = Type::String;
                return e.type;
            case ExprKind::Identifier: {
                const Type t = LookupIdentifier(e.text);
                if (t == Type::Unknown) {
                    Report(DiagCode::UndefinedIdentifier, e.loc, "undefined identifier '" + e.text + "'");
                }
                e.type = t;
                return e.type;
            }
            case ExprKind::Binary:
                return CheckBinary(e);
            case ExprKind::Unary:
                return CheckUnary(e);
            case ExprKind::Call:
                return CheckCall(e);
            case ExprKind::Member:
                return CheckMember(e);
        }
        e.type = Type::Unknown;
        return e.type;
    }

    // Folds a Binary/Unary node with already-literal operands into a
    // single literal node in place (recursing into children first).
    // Purely a diagnostics aid -- it's what lets `x / (1 - 1)` be caught
    // as division-by-zero, not just a bare `x / 0` -- not a general
    // optimizer; LLVM's own pipeline (GS4) does the real constant
    // folding once this compiles to IR.
    static void FoldConstants(Expr& e) {
        if (e.kind == ExprKind::Unary) {
            FoldConstants(*e.lhs);
            if (e.unaryOp == UnaryOp::Neg && e.lhs->kind == ExprKind::IntLiteral) {
                const int64_t v = e.lhs->intValue;
                e.kind = ExprKind::IntLiteral;
                e.intValue = -v;
            } else if (e.unaryOp == UnaryOp::Neg && e.lhs->kind == ExprKind::FloatLiteral) {
                const float v = e.lhs->floatValue;
                e.kind = ExprKind::FloatLiteral;
                e.floatValue = -v;
            } else if (e.unaryOp == UnaryOp::Not && e.lhs->kind == ExprKind::BoolLiteral) {
                const bool v = e.lhs->boolValue;
                e.kind = ExprKind::BoolLiteral;
                e.boolValue = !v;
            }
            return;
        }
        if (e.kind == ExprKind::Binary) {
            FoldConstants(*e.lhs);
            FoldConstants(*e.rhs);
            if (e.lhs->kind == ExprKind::IntLiteral && e.rhs->kind == ExprKind::IntLiteral) {
                const int64_t a = e.lhs->intValue;
                const int64_t b = e.rhs->intValue;
                switch (e.binaryOp) {
                    case BinaryOp::Add: e.kind = ExprKind::IntLiteral; e.intValue = a + b; return;
                    case BinaryOp::Sub: e.kind = ExprKind::IntLiteral; e.intValue = a - b; return;
                    case BinaryOp::Mul: e.kind = ExprKind::IntLiteral; e.intValue = a * b; return;
                    case BinaryOp::Div:
                        if (b != 0) { e.kind = ExprKind::IntLiteral; e.intValue = a / b; }
                        return;
                    case BinaryOp::Mod:
                        if (b != 0) { e.kind = ExprKind::IntLiteral; e.intValue = a % b; }
                        return;
                    default: return; // comparisons/logical ops don't need folding for anything sema checks.
                }
            }
            if (e.lhs->kind == ExprKind::FloatLiteral && e.rhs->kind == ExprKind::FloatLiteral) {
                const float a = e.lhs->floatValue;
                const float b = e.rhs->floatValue;
                switch (e.binaryOp) {
                    case BinaryOp::Add: e.kind = ExprKind::FloatLiteral; e.floatValue = a + b; return;
                    case BinaryOp::Sub: e.kind = ExprKind::FloatLiteral; e.floatValue = a - b; return;
                    case BinaryOp::Mul: e.kind = ExprKind::FloatLiteral; e.floatValue = a * b; return;
                    case BinaryOp::Div:
                        // Float division by a folded zero is IEEE inf/nan, not folded away here --
                        // CheckBinary's divisor check below still flags it as CEL2020.
                        e.kind = ExprKind::FloatLiteral;
                        e.floatValue = a / b;
                        return;
                    default: return;
                }
            }
        }
    }

    static void CheckDivisionByZero(DiagnosticEngine& diagnostics, const Expr& divisor) {
        const bool intZero = divisor.kind == ExprKind::IntLiteral && divisor.intValue == 0;
        const bool floatZero = divisor.kind == ExprKind::FloatLiteral && divisor.floatValue == 0.0f;
        if (intZero || floatZero) {
            diagnostics.Report(DiagCode::DivisionByZero, DiagnosticSeverity::Error, divisor.loc,
                                "division by literal zero");
        }
    }

    Type CheckBinary(Expr& e) {
        const Type lhs = CheckExpr(*e.lhs);
        const Type rhs = CheckExpr(*e.rhs);
        if (lhs == Type::Unknown || rhs == Type::Unknown) {
            e.type = Type::Unknown;
            return e.type;
        }

        bool ok = false;
        Type result = Type::Unknown;
        switch (e.binaryOp) {
            case BinaryOp::Add:
            case BinaryOp::Sub:
                if (lhs == Type::Int && rhs == Type::Int) { ok = true; result = Type::Int; }
                else if (lhs == Type::Float && rhs == Type::Float) { ok = true; result = Type::Float; }
                else if (lhs == rhs && (IsVecType(lhs) || IsMatType(lhs))) { ok = true; result = lhs; }
                break;
            case BinaryOp::Mul:
                if (lhs == Type::Int && rhs == Type::Int) { ok = true; result = Type::Int; }
                else if (lhs == Type::Float && rhs == Type::Float) { ok = true; result = Type::Float; }
                else if (IsVecType(lhs) && rhs == Type::Float) { ok = true; result = lhs; }
                else if (lhs == Type::Float && IsVecType(rhs)) { ok = true; result = rhs; }
                // Scalar * matrix (componentwise) -- same shape as scalar * vector above.
                else if (IsMatType(lhs) && rhs == Type::Float) { ok = true; result = lhs; }
                else if (lhs == Type::Float && IsMatType(rhs)) { ok = true; result = rhs; }
                // Matrix * matrix (same size) -- real matrix multiplication, NOT
                // componentwise, per standard linear-algebra convention (unlike
                // vector '*', which IS componentwise -- vectors have no
                // multiplication operator of their own; dot/cross are named
                // functions instead, so there's no existing precedent this
                // would collide with).
                else if (IsMatType(lhs) && lhs == rhs) { ok = true; result = lhs; }
                // Matrix * vector (matching size) -- matrix-vector product.
                else if (IsMatType(lhs) && rhs == MatVecType(lhs)) { ok = true; result = rhs; }
                break;
            case BinaryOp::Div:
                if (lhs == Type::Int && rhs == Type::Int) {
                    CheckDivisionByZero(diagnostics_, *e.rhs);
                    ok = true; result = Type::Int;
                } else if (lhs == Type::Float && rhs == Type::Float) { ok = true; result = Type::Float; }
                else if (IsVecType(lhs) && rhs == Type::Float) { ok = true; result = lhs; }
                break;
            case BinaryOp::Mod:
                if (lhs == Type::Int && rhs == Type::Int) {
                    CheckDivisionByZero(diagnostics_, *e.rhs);
                    ok = true; result = Type::Int;
                }
                break;
            case BinaryOp::Eq:
            case BinaryOp::Neq:
                if (lhs == rhs && (lhs == Type::Int || lhs == Type::Float || lhs == Type::Bool || IsVecType(lhs) ||
                                    IsMatType(lhs) || lhs == Type::Entity)) {
                    ok = true; result = Type::Bool;
                }
                break;
            case BinaryOp::Lt:
            case BinaryOp::Gt:
            case BinaryOp::Le:
            case BinaryOp::Ge:
                if ((lhs == Type::Int && rhs == Type::Int) || (lhs == Type::Float && rhs == Type::Float)) {
                    ok = true; result = Type::Bool;
                }
                break;
            case BinaryOp::And:
            case BinaryOp::Or:
                if (lhs == Type::Bool && rhs == Type::Bool) { ok = true; result = Type::Bool; }
                break;
        }

        if (!ok) {
            Report(DiagCode::InvalidOperator, e.loc,
                   std::string("operator '") + BinaryOpSpelling(e.binaryOp) + "' is not defined for " + ToString(lhs) +
                       " and " + ToString(rhs));
            e.type = Type::Unknown;
            return e.type;
        }
        e.type = result;
        return e.type;
    }

    Type CheckUnary(Expr& e) {
        const Type operand = CheckExpr(*e.lhs);
        if (operand == Type::Unknown) {
            e.type = Type::Unknown;
            return e.type;
        }
        if (e.unaryOp == UnaryOp::Neg &&
            (operand == Type::Int || operand == Type::Float || IsVecType(operand) || IsMatType(operand))) {
            e.type = operand;
            return e.type;
        }
        if (e.unaryOp == UnaryOp::Not && operand == Type::Bool) {
            e.type = Type::Bool;
            return e.type;
        }
        Report(DiagCode::InvalidOperator, e.loc,
               std::string("unary '") + (e.unaryOp == UnaryOp::Neg ? "-" : "!") + "' is not defined for " +
                   ToString(operand));
        e.type = Type::Unknown;
        return e.type;
    }

    Type CheckCall(Expr& e) {
        // v1 has no function values/closures -- the callee must be a
        // bare identifier naming a known function or intrinsic, checked
        // directly rather than by computing a "function type" for e.lhs.
        if (e.lhs->kind != ExprKind::Identifier) {
            Report(DiagCode::UndefinedFunction, e.lhs->loc, "call target must be a function name");
            for (Expr* arg : e.args) {
                CheckExpr(*arg);
            }
            e.type = Type::Unknown;
            return e.type;
        }

        const auto it = functions_.find(e.lhs->text);
        if (it == functions_.end()) {
            Report(DiagCode::UndefinedFunction, e.lhs->loc, "undefined function '" + e.lhs->text + "'");
            for (Expr* arg : e.args) {
                CheckExpr(*arg);
            }
            e.type = Type::Unknown;
            return e.type;
        }

        const FunctionSignature& sig = it->second;
        e.lhs->type = Type::Unknown; // the callee identifier itself has no expression type -- v1 has no function values.

        // GS-Interop: a call to a known intrinsic outside this host's
        // capability profile is rejected here -- compile-time, at the
        // exact call site -- in addition to the defense-in-depth JIT
        // symbol-resolution filter (RegisterAbiTrampolines). Reported,
        // not returned early: still validates arity/argument types below
        // so a script with both a domain violation and a real type error
        // sees both, matching every other diagnostic in this function.
        if (const auto domainIt = intrinsicDomains_.find(e.lhs->text);
            domainIt != intrinsicDomains_.end() && !allowedDomains_.Contains(domainIt->second)) {
            Report(DiagCode::IntrinsicNotAvailableInDomain, e.loc,
                   "intrinsic '" + e.lhs->text + "' is not available in this host's domain profile (requires domain '" +
                       ToString(domainIt->second) + "')");
        }

        if (e.args.size() != sig.paramTypes.size()) {
            Report(DiagCode::WrongArgumentCount, e.loc,
                   "'" + e.lhs->text + "' expects " + std::to_string(sig.paramTypes.size()) + " argument(s), got " +
                       std::to_string(e.args.size()));
            for (Expr* arg : e.args) {
                CheckExpr(*arg);
            }
            e.type = sig.returnType; // still return the declared type so callers don't cascade spurious errors.
            return e.type;
        }

        for (std::size_t i = 0; i < e.args.size(); ++i) {
            Expr& arg = *e.args[i];
            const Type expected = sig.paramTypes[i];
            if (expected == Type::String) {
                if (arg.kind != ExprKind::StringLiteral) {
                    Report(DiagCode::NonLiteralStringArgument, arg.loc,
                           "argument " + std::to_string(i + 1) + " to '" + e.lhs->text + "' must be a string literal");
                }
                arg.type = Type::String;
                continue;
            }
            const Type actual = CheckExpr(arg);
            if (actual != Type::Unknown && actual != expected) {
                Report(DiagCode::TypeMismatch, arg.loc,
                       "argument " + std::to_string(i + 1) + " to '" + e.lhs->text + "' expects " + ToString(expected) +
                           ", got " + ToString(actual));
            }
        }

        e.type = sig.returnType;
        return e.type;
    }

    Type CheckMember(Expr& e) {
        const Type base = CheckExpr(*e.lhs);
        if (base == Type::Unknown) {
            e.type = Type::Unknown;
            return e.type;
        }
        if (!IsVecType(base)) {
            Report(DiagCode::NoMemberAccess, e.loc, std::string(ToString(base)) + " has no member '." + e.text + "'");
            e.type = Type::Unknown;
            return e.type;
        }
        static const char* const kFieldLetters = "xyzw";
        const int componentCount = VecComponentCount(base);
        const bool validField = e.text.size() == 1 &&
                                 std::string(kFieldLetters, static_cast<std::size_t>(componentCount)).find(e.text) !=
                                     std::string::npos;
        if (!validField) {
            const std::string expected(kFieldLetters, static_cast<std::size_t>(componentCount));
            std::string expectedList;
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (i != 0) expectedList += ", ";
                expectedList += ".";
                expectedList += expected[i];
            }
            Report(DiagCode::UnknownMember, e.loc,
                   std::string(ToString(base)) + " has no member '." + e.text + "' (expected " + expectedList + ")");
            e.type = Type::Unknown;
            return e.type;
        }
        e.type = Type::Float;
        return e.type;
    }

    DiagnosticEngine& diagnostics_;
    std::unordered_map<std::string, FunctionSignature> functions_;
    std::unordered_set<std::string> intrinsicNames_;
    std::unordered_map<std::string, IntrinsicDomain> intrinsicDomains_; // GS-Interop.
    IntrinsicDomainSet allowedDomains_ = IntrinsicDomainSet::All();     // GS-Interop.
    std::unordered_map<std::string, Type> globals_;
    std::vector<std::unordered_map<std::string, Type>> scopes_;
};

} // namespace

bool AnalyzeProgram(Program& program, DiagnosticEngine& diagnostics, IntrinsicDomainSet allowedDomains) {
    Sema sema(diagnostics, allowedDomains);
    return sema.Analyze(program);
}

} // namespace ce::lang
