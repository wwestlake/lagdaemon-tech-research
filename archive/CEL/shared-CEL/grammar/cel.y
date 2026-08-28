%require "3.8"
%language "c++"
%skeleton "lalr1.cc"

%define api.namespace {ce::lang}
%define api.parser.class {Parser}
%define api.token.constructor
%define api.value.type variant
%define api.location.file "location.hh"
%define parse.error detailed
%define parse.trace
%locations

// The concrete, mechanical proof this grammar is genuinely LALR(1), not
// merely "parses the test suite": any shift/reduce or reduce/reduce
// conflict fails the build outright. Dangling-else is resolved by
// structurally splitting every statement production into "closed"
// (definitely has a matching else, or can't have one) and "open"
// (an if/while/for whose innermost statement is still open) forms --
// see closed_stmt/open_stmt below -- rather than relying on Bison's
// default shift-wins conflict resolution, which %expect 0 would reject
// anyway. GLR is banned by project policy: it would silently reintroduce
// ambiguity tolerance the whole point of this exercise is to rule out.
%expect 0

%code requires {
    #include <cstdint>
    #include <string>
    #include "lang/ast.h"
    #include "lang/diagnostics.h"

    namespace ce::lang {
    class Lexer;
    }
}

%param { ce::lang::Lexer& lexer }
%parse-param { ce::lang::AstArena& arena }
%parse-param { ce::lang::DiagnosticEngine& diagnostics }
%parse-param { ce::lang::Program*& result }

%code {
    #include "lang/diagnostics.h"
    #include "lang/lexer.h"

    namespace ce::lang {
    static Parser::symbol_type yylex(Lexer& lexer) { return lexer.NextToken(); }

    namespace {
    SourceLocation ToSourceLocation(const Parser::location_type& loc) {
        return SourceLocation{ loc.begin.line, loc.begin.column };
    }
    } // namespace
    } // namespace ce::lang
}

%token
    VAR "var" FUNC "func" IF "if" ELSE "else" WHILE "while" FOR "for"
    BREAK "break" CONTINUE "continue" RETURN "return" TRUE "true" FALSE "false"
    LBRACE "{" RBRACE "}" LPAREN "(" RPAREN ")"
    COMMA "," SEMI ";" COLON ":" DOT "." ARROW "->" ASSIGN "="
    PLUSEQ "+=" MINUSEQ "-=" STAREQ "*=" SLASHEQ "/="
    EQ "==" NEQ "!=" LT "<" GT ">" LE "<=" GE ">="
    ANDAND "&&" OROR "||"
    PLUS "+" MINUS "-" STAR "*" SLASH "/" PERCENT "%" NOT "!"
;

%token <int64_t> INT_LITERAL
%token <float> FLOAT_LITERAL
%token <std::string> STRING_LITERAL
%token <std::string> IDENT

%type <ce::lang::Expr*> expr or_expr and_expr eq_expr rel_expr add_expr mul_expr unary_expr postfix_expr primary_expr
%type <ce::lang::Stmt*> stmt closed_stmt open_stmt simple_stmt block
%type <ce::lang::Stmt*> for_init_opt for_step_opt assign_no_semi var_decl_no_semi
%type <ce::lang::Stmt*> var_decl_stmt assign_stmt
%type <std::vector<ce::lang::Stmt*>> stmt_list
%type <std::vector<ce::lang::Expr*>> arg_list arg_list_opt
%type <ce::lang::Param> param
%type <std::vector<ce::lang::Param>> param_list param_list_opt
%type <ce::lang::FuncDecl*> func_decl
%type <ce::lang::GlobalVarDecl*> global_var_decl
%type <ce::lang::Decl*> decl
%type <std::vector<ce::lang::Decl*>> decl_list

%%

program:
    decl_list {
        Program* prog = arena.NewProgram();
        prog->decls = std::move($1);
        result = prog;
    }
;

decl_list:
    %empty { $$ = {}; }
  | decl_list decl { $$ = std::move($1); $$.push_back($2); }
;

decl:
    global_var_decl { $$ = arena.NewDecl(DeclKind::Var); $$->varDecl = $1; }
  | func_decl       { $$ = arena.NewDecl(DeclKind::Func); $$->funcDecl = $1; }
;

global_var_decl:
    "var" IDENT ":" IDENT "=" expr ";" {
        $$ = arena.NewGlobalVarDecl();
        $$->name = $2; $$->declaredType = $4; $$->initExpr = $6; $$->loc = ToSourceLocation(@1);
    }
  | "var" IDENT "=" expr ";" {
        $$ = arena.NewGlobalVarDecl();
        $$->name = $2; $$->initExpr = $4; $$->loc = ToSourceLocation(@1);
    }
;

func_decl:
    "func" IDENT "(" param_list_opt ")" "->" IDENT block {
        $$ = arena.NewFuncDecl();
        $$->name = $2; $$->params = std::move($4); $$->returnType = $7; $$->body = $8; $$->loc = ToSourceLocation(@1);
    }
  | "func" IDENT "(" param_list_opt ")" block {
        $$ = arena.NewFuncDecl();
        $$->name = $2; $$->params = std::move($4); $$->body = $6; $$->loc = ToSourceLocation(@1);
    }
;

param_list_opt:
    %empty     { $$ = {}; }
  | param_list { $$ = std::move($1); }
;

param_list:
    param                  { $$ = {}; $$.push_back(std::move($1)); }
  | param_list "," param   { $$ = std::move($1); $$.push_back(std::move($3)); }
;

param:
    IDENT ":" IDENT { $$ = Param{ $1, $3, ToSourceLocation(@1) }; }
;

block:
    "{" stmt_list "}" {
        $$ = arena.NewStmt(StmtKind::Block, ToSourceLocation(@1));
        $$->statements = std::move($2);
    }
;

stmt_list:
    %empty              { $$ = {}; }
  | stmt_list stmt       { $$ = std::move($1); $$.push_back($2); }
;

// Dangling-else-free statement grammar (the standard "matched/
// unmatched" restructuring): `stmt` is either fully closed (every if
// inside it has an else) or open (its rightmost branch is an
// if/while/for with no else yet). An `if` can only nest an open
// statement in its else-less form, and can only nest a closed statement
// before its `else` -- so `if (a) if (b) s1; else s2;` has exactly one
// parse (the else binds to the inner if), with no conflict for Bison to
// resolve either way.
stmt:
    open_stmt   { $$ = $1; }
  | closed_stmt { $$ = $1; }
;

closed_stmt:
    simple_stmt { $$ = $1; }
  | block       { $$ = $1; }
  | "if" "(" expr ")" closed_stmt "else" closed_stmt {
        $$ = arena.NewStmt(StmtKind::If, ToSourceLocation(@1));
        $$->condition = $3; $$->thenBranch = $5; $$->elseBranch = $7;
    }
  | "while" "(" expr ")" closed_stmt {
        $$ = arena.NewStmt(StmtKind::While, ToSourceLocation(@1));
        $$->condition = $3; $$->body = $5;
    }
  | "for" "(" for_init_opt ";" expr ";" for_step_opt ")" closed_stmt {
        $$ = arena.NewStmt(StmtKind::For, ToSourceLocation(@1));
        $$->forInit = $3; $$->forCond = $5; $$->forStep = $7; $$->body = $9;
    }
;

open_stmt:
    "if" "(" expr ")" stmt {
        $$ = arena.NewStmt(StmtKind::If, ToSourceLocation(@1));
        $$->condition = $3; $$->thenBranch = $5;
    }
  | "if" "(" expr ")" closed_stmt "else" open_stmt {
        $$ = arena.NewStmt(StmtKind::If, ToSourceLocation(@1));
        $$->condition = $3; $$->thenBranch = $5; $$->elseBranch = $7;
    }
  | "while" "(" expr ")" open_stmt {
        $$ = arena.NewStmt(StmtKind::While, ToSourceLocation(@1));
        $$->condition = $3; $$->body = $5;
    }
  | "for" "(" for_init_opt ";" expr ";" for_step_opt ")" open_stmt {
        $$ = arena.NewStmt(StmtKind::For, ToSourceLocation(@1));
        $$->forInit = $3; $$->forCond = $5; $$->forStep = $7; $$->body = $9;
    }
;

simple_stmt:
    var_decl_stmt   { $$ = $1; }
  | assign_stmt     { $$ = $1; }
  | "break" ";"     { $$ = arena.NewStmt(StmtKind::Break, ToSourceLocation(@1)); }
  | "continue" ";"  { $$ = arena.NewStmt(StmtKind::Continue, ToSourceLocation(@1)); }
  | "return" ";"    { $$ = arena.NewStmt(StmtKind::Return, ToSourceLocation(@1)); }
  | "return" expr ";" {
        $$ = arena.NewStmt(StmtKind::Return, ToSourceLocation(@1));
        $$->returnValue = $2;
    }
  | expr ";" {
        $$ = arena.NewStmt(StmtKind::ExprStmt, $1->loc);
        $$->expr = $1;
    }
;

var_decl_stmt:
    var_decl_no_semi ";" { $$ = $1; }
;

var_decl_no_semi:
    "var" IDENT ":" IDENT "=" expr {
        $$ = arena.NewStmt(StmtKind::VarDecl, ToSourceLocation(@1));
        $$->name = $2; $$->declaredType = $4; $$->initExpr = $6;
    }
  | "var" IDENT "=" expr {
        $$ = arena.NewStmt(StmtKind::VarDecl, ToSourceLocation(@1));
        $$->name = $2; $$->initExpr = $4;
    }
;

assign_stmt:
    assign_no_semi ";" { $$ = $1; }
;

// The assignment target is a general `expr`, not a hand-rolled
// `IDENT ("." IDENT)?` production, specifically so it shares postfix_expr's
// existing grammar path with ordinary member-access expressions instead
// of duplicating it. Two separate rules both trying to consume an
// `IDENT "." IDENT` prefix (one for "this is an assignment target", one
// via postfix_expr for "this is a member-access expression") is exactly
// what produced a real shift/reduce conflict during GS2 development --
// LALR(1) can't tell, one token past "IDENT .", whether it's heading
// into `IDENT "." IDENT "=" ...` or `IDENT "." IDENT ";"` (a no-op
// expression statement) without more lookahead than it has. Folding the
// target into `expr` removes the duplication: there's only one path to
// parse `p.x`, and the choice between "this was an assignment" and
// "this was a bare expression statement" only has to be made by the
// single token *after* that shared expr (`=`/`+=`/... vs `;`), which
// LALR(1) handles natively. Whether the resulting target expr is
// actually a valid lvalue (an Identifier, or a Member of one) is a
// semantic question -- see ast.h's Stmt::assignTarget comment -- left to
// sema (GS3), not enforced by the grammar.
assign_no_semi:
    expr "=" expr {
        $$ = arena.NewStmt(StmtKind::Assign, $1->loc);
        $$->assignTarget = $1; $$->assignOp = AssignOp::Assign; $$->assignValue = $3;
    }
  | expr "+=" expr {
        $$ = arena.NewStmt(StmtKind::Assign, $1->loc);
        $$->assignTarget = $1; $$->assignOp = AssignOp::AddAssign; $$->assignValue = $3;
    }
  | expr "-=" expr {
        $$ = arena.NewStmt(StmtKind::Assign, $1->loc);
        $$->assignTarget = $1; $$->assignOp = AssignOp::SubAssign; $$->assignValue = $3;
    }
  | expr "*=" expr {
        $$ = arena.NewStmt(StmtKind::Assign, $1->loc);
        $$->assignTarget = $1; $$->assignOp = AssignOp::MulAssign; $$->assignValue = $3;
    }
  | expr "/=" expr {
        $$ = arena.NewStmt(StmtKind::Assign, $1->loc);
        $$->assignTarget = $1; $$->assignOp = AssignOp::DivAssign; $$->assignValue = $3;
    }
;

for_init_opt:
    %empty            { $$ = nullptr; }
  | var_decl_no_semi  { $$ = $1; }
  | assign_no_semi    { $$ = $1; }
;

for_step_opt:
    %empty          { $$ = nullptr; }
  | assign_no_semi  { $$ = $1; }
;

arg_list_opt:
    %empty    { $$ = {}; }
  | arg_list  { $$ = std::move($1); }
;

arg_list:
    expr                { $$ = {}; $$.push_back($1); }
  | arg_list "," expr   { $$ = std::move($1); $$.push_back($3); }
;

// Expression precedence is entirely structural (each level only ever
// refers to itself and the next-tighter level), not resolved via Bison
// %left/%right conflict-breaking -- the textbook-correct way to encode
// precedence in an LALR(1) grammar without leaving Bison to arbitrate
// anything. This is also what makes "." and "(" (postfix, tightest
// binding) unambiguous against unary/binary operators without needing
// their own precedence declarations: postfix_expr sits structurally
// between unary_expr and primary_expr, so `-a.b` can only ever parse as
// `-(a.b)`, never `(-a).b`, by construction.
expr: or_expr { $$ = $1; } ;

or_expr:
    or_expr "||" and_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Or; $$->lhs = $1; $$->rhs = $3;
    }
  | and_expr { $$ = $1; }
;

and_expr:
    and_expr "&&" eq_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::And; $$->lhs = $1; $$->rhs = $3;
    }
  | eq_expr { $$ = $1; }
;

eq_expr:
    eq_expr "==" rel_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Eq; $$->lhs = $1; $$->rhs = $3;
    }
  | eq_expr "!=" rel_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Neq; $$->lhs = $1; $$->rhs = $3;
    }
  | rel_expr { $$ = $1; }
;

rel_expr:
    rel_expr "<" add_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Lt; $$->lhs = $1; $$->rhs = $3;
    }
  | rel_expr ">" add_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Gt; $$->lhs = $1; $$->rhs = $3;
    }
  | rel_expr "<=" add_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Le; $$->lhs = $1; $$->rhs = $3;
    }
  | rel_expr ">=" add_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Ge; $$->lhs = $1; $$->rhs = $3;
    }
  | add_expr { $$ = $1; }
;

add_expr:
    add_expr "+" mul_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Add; $$->lhs = $1; $$->rhs = $3;
    }
  | add_expr "-" mul_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Sub; $$->lhs = $1; $$->rhs = $3;
    }
  | mul_expr { $$ = $1; }
;

mul_expr:
    mul_expr "*" unary_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Mul; $$->lhs = $1; $$->rhs = $3;
    }
  | mul_expr "/" unary_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Div; $$->lhs = $1; $$->rhs = $3;
    }
  | mul_expr "%" unary_expr {
        $$ = arena.NewExpr(ExprKind::Binary, ToSourceLocation(@1)); $$->binaryOp = BinaryOp::Mod; $$->lhs = $1; $$->rhs = $3;
    }
  | unary_expr { $$ = $1; }
;

unary_expr:
    "-" unary_expr {
        $$ = arena.NewExpr(ExprKind::Unary, ToSourceLocation(@1)); $$->unaryOp = UnaryOp::Neg; $$->lhs = $2;
    }
  | "!" unary_expr {
        $$ = arena.NewExpr(ExprKind::Unary, ToSourceLocation(@1)); $$->unaryOp = UnaryOp::Not; $$->lhs = $2;
    }
  | postfix_expr { $$ = $1; }
;

postfix_expr:
    postfix_expr "." IDENT {
        $$ = arena.NewExpr(ExprKind::Member, $1->loc); $$->lhs = $1; $$->text = $3;
    }
  | postfix_expr "(" arg_list_opt ")" {
        $$ = arena.NewExpr(ExprKind::Call, $1->loc); $$->lhs = $1; $$->args = std::move($3);
    }
  | primary_expr { $$ = $1; }
;

primary_expr:
    "(" expr ")" { $$ = $2; }
  | IDENT {
        $$ = arena.NewExpr(ExprKind::Identifier, ToSourceLocation(@1)); $$->text = $1;
    }
  | INT_LITERAL {
        $$ = arena.NewExpr(ExprKind::IntLiteral, ToSourceLocation(@1)); $$->intValue = $1;
    }
  | FLOAT_LITERAL {
        $$ = arena.NewExpr(ExprKind::FloatLiteral, ToSourceLocation(@1)); $$->floatValue = $1;
    }
  | STRING_LITERAL {
        $$ = arena.NewExpr(ExprKind::StringLiteral, ToSourceLocation(@1)); $$->text = $1;
    }
  | "true" {
        $$ = arena.NewExpr(ExprKind::BoolLiteral, ToSourceLocation(@1)); $$->boolValue = true;
    }
  | "false" {
        $$ = arena.NewExpr(ExprKind::BoolLiteral, ToSourceLocation(@1)); $$->boolValue = false;
    }
;

%%

namespace ce::lang {

void Parser::error(const location_type& loc, const std::string& msg) {
    diagnostics.Report(DiagCode::SyntaxError, DiagnosticSeverity::Error, ToSourceLocation(loc), msg);
}

} // namespace ce::lang
