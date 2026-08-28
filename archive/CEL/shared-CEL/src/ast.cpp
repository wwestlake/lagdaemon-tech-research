#include "lang/ast.h"

namespace ce::lang {

Expr* AstArena::NewExpr(ExprKind kind, SourceLocation loc) {
    exprs_.push_back(std::make_unique<Expr>());
    Expr* e = exprs_.back().get();
    e->kind = kind;
    e->loc = loc;
    return e;
}

Stmt* AstArena::NewStmt(StmtKind kind, SourceLocation loc) {
    stmts_.push_back(std::make_unique<Stmt>());
    Stmt* s = stmts_.back().get();
    s->kind = kind;
    s->loc = loc;
    return s;
}

FuncDecl* AstArena::NewFuncDecl() {
    funcDecls_.push_back(std::make_unique<FuncDecl>());
    return funcDecls_.back().get();
}

GlobalVarDecl* AstArena::NewGlobalVarDecl() {
    globalVarDecls_.push_back(std::make_unique<GlobalVarDecl>());
    return globalVarDecls_.back().get();
}

Decl* AstArena::NewDecl(DeclKind kind) {
    decls_.push_back(std::make_unique<Decl>());
    Decl* d = decls_.back().get();
    d->kind = kind;
    return d;
}

Program* AstArena::NewProgram() {
    programs_.push_back(std::make_unique<Program>());
    return programs_.back().get();
}

} // namespace ce::lang
