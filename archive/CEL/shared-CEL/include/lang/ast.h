#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lang/source_location.h"
#include "lang/type.h"

namespace ce::lang {

// AST nodes are plain structs with raw-pointer children, owned by an
// AstArena (below) rather than by std::unique_ptr child-to-parent
// ownership. This is deliberate: a Bison `%define api.value.type
// variant` grammar has to move semantic values around a lot as rules
// reduce, and move-only unique_ptr-in-a-variant is a well-known source
// of subtle bugs in hand-written grammars. Raw pointers are trivially
// copyable, so the generated parser code has nothing tricky to get
// right -- the arena is what actually keeps every node alive for the
// AST's lifetime, exactly like a bump allocator in a real compiler
// front end.

enum class BinaryOp { Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Gt, Le, Ge, And, Or };
enum class UnaryOp { Neg, Not };
enum class AssignOp { Assign, AddAssign, SubAssign, MulAssign, DivAssign };

enum class ExprKind { IntLiteral, FloatLiteral, BoolLiteral, StringLiteral, Identifier, Binary, Unary, Call, Member };

struct Expr {
    ExprKind kind;
    SourceLocation loc;

    // Set by sema (GS3); Type::Unknown until then (and left Unknown on
    // any subtree sema gave up on after reporting an error, so codegen
    // never has to guess whether a type was actually resolved).
    Type type = Type::Unknown;

    int64_t intValue = 0;
    float floatValue = 0.0f;
    bool boolValue = false;
    // StringLiteral value / Identifier name / Member field name / Call callee name.
    std::string text;

    BinaryOp binaryOp{};
    UnaryOp unaryOp{};

    Expr* lhs = nullptr; // Binary lhs, Unary operand, Member base, Call callee expr.
    Expr* rhs = nullptr; // Binary rhs only.
    std::vector<Expr*> args; // Call arguments.
};

enum class StmtKind { VarDecl, Assign, If, While, For, Break, Continue, Return, ExprStmt, Block };

struct Stmt {
    StmtKind kind;
    SourceLocation loc;

    // VarDecl (also reused for a for-loop's init clause).
    std::string name;
    std::string declaredType; // as spelled in source; empty => inferred from initExpr.
    Type resolvedType = Type::Unknown; // set by sema: declaredType parsed, or initExpr's inferred type.
    Expr* initExpr = nullptr;

    // Assign (also reused for a for-loop's step clause). assignTarget is
    // parsed as a general expr (so it shares postfix_expr's grammar path
    // with ordinary member-access expressions, which resolves a real
    // parser ambiguity -- see cel.y's comment above assign_no_semi) but
    // is only ever meaningful when it's actually an lvalue: an
    // Identifier, or a Member whose base is an Identifier (one level,
    // per the language spec -- v1 has no nested aggregates for a deeper
    // target to make sense against). Sema (GS3) is what actually
    // enforces this; the grammar accepts any expr here.
    Expr* assignTarget = nullptr;
    AssignOp assignOp{};
    Expr* assignValue = nullptr;

    // If
    Expr* condition = nullptr;
    Stmt* thenBranch = nullptr;
    Stmt* elseBranch = nullptr; // null => no else.

    // While/For
    Stmt* forInit = nullptr; // VarDecl- or Assign-kind Stmt, or null.
    Expr* forCond = nullptr; // null => `true`.
    Stmt* forStep = nullptr; // Assign-kind Stmt, or null.
    Stmt* body = nullptr;    // While/For loop body; also used by If for... no, If uses thenBranch/elseBranch above.

    // Return
    Expr* returnValue = nullptr; // null => bare `return;`.

    // ExprStmt
    Expr* expr = nullptr;

    // Block
    std::vector<Stmt*> statements;
};

struct Param {
    std::string name;
    std::string type;
    SourceLocation loc;
};

struct FuncDecl {
    std::string name;
    std::vector<Param> params;
    std::string returnType; // empty => void.
    Stmt* body = nullptr;   // a Block-kind Stmt.
    SourceLocation loc;
};

struct GlobalVarDecl {
    std::string name;
    std::string declaredType; // as spelled in source; empty => inferred from initExpr.
    Type resolvedType = Type::Unknown; // set by sema: declaredType parsed, or initExpr's inferred type.
    Expr* initExpr = nullptr;
    SourceLocation loc;
};

enum class DeclKind { Var, Func };

struct Decl {
    DeclKind kind;
    GlobalVarDecl* varDecl = nullptr;
    FuncDecl* funcDecl = nullptr;
};

struct Program {
    std::vector<Decl*> decls;
};

// Owns every AST node's actual storage. A Program (and everything it
// points to) is only valid as long as the AstArena that built it stays
// alive -- callers keep the two together (see Compiler::Parse in
// compiler.h).
class AstArena {
public:
    Expr* NewExpr(ExprKind kind, SourceLocation loc);
    Stmt* NewStmt(StmtKind kind, SourceLocation loc);
    FuncDecl* NewFuncDecl();
    GlobalVarDecl* NewGlobalVarDecl();
    Decl* NewDecl(DeclKind kind);
    Program* NewProgram();

private:
    std::vector<std::unique_ptr<Expr>> exprs_;
    std::vector<std::unique_ptr<Stmt>> stmts_;
    std::vector<std::unique_ptr<FuncDecl>> funcDecls_;
    std::vector<std::unique_ptr<GlobalVarDecl>> globalVarDecls_;
    std::vector<std::unique_ptr<Decl>> decls_;
    std::vector<std::unique_ptr<Program>> programs_; // always exactly one in practice; vector keeps ownership uniform.
};

} // namespace ce::lang
