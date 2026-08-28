#include "lang/ast_printer.h"

namespace ce::lang {

namespace {

const char* ToString(BinaryOp op) {
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

const char* ToString(UnaryOp op) {
    switch (op) {
        case UnaryOp::Neg: return "-";
        case UnaryOp::Not: return "!";
    }
    return "?";
}

const char* ToString(AssignOp op) {
    switch (op) {
        case AssignOp::Assign: return "=";
        case AssignOp::AddAssign: return "+=";
        case AssignOp::SubAssign: return "-=";
        case AssignOp::MulAssign: return "*=";
        case AssignOp::DivAssign: return "/=";
    }
    return "?";
}

class Printer {
public:
    explicit Printer(std::ostream& out) : out_(out) {}

    void PrintProgram(const Program& program) {
        Line("Program");
        Indent();
        for (const Decl* decl : program.decls) {
            PrintDecl(*decl);
        }
        Dedent();
    }

private:
    void PrintDecl(const Decl& decl) {
        if (decl.kind == DeclKind::Var) {
            PrintGlobalVarDecl(*decl.varDecl);
        } else {
            PrintFuncDecl(*decl.funcDecl);
        }
    }

    void PrintGlobalVarDecl(const GlobalVarDecl& v) {
        Line("GlobalVarDecl " + v.name + ": " + (v.declaredType.empty() ? "<inferred>" : v.declaredType));
        Indent();
        PrintExpr(*v.initExpr);
        Dedent();
    }

    void PrintFuncDecl(const FuncDecl& f) {
        std::string header = "FuncDecl " + f.name + "(";
        for (std::size_t i = 0; i < f.params.size(); ++i) {
            if (i > 0) {
                header += ", ";
            }
            header += f.params[i].name + ": " + f.params[i].type;
        }
        header += ") -> " + (f.returnType.empty() ? "void" : f.returnType);
        Line(header);
        Indent();
        PrintStmt(*f.body);
        Dedent();
    }

    void PrintStmt(const Stmt& s) {
        switch (s.kind) {
            case StmtKind::Block:
                Line("Block");
                Indent();
                for (const Stmt* child : s.statements) {
                    PrintStmt(*child);
                }
                Dedent();
                break;
            case StmtKind::VarDecl:
                Line("VarDecl " + s.name + ": " + (s.declaredType.empty() ? "<inferred>" : s.declaredType));
                Indent();
                PrintExpr(*s.initExpr);
                Dedent();
                break;
            case StmtKind::Assign:
                Line(std::string("Assign ") + ToString(s.assignOp));
                Indent();
                Line("Target");
                Indent();
                PrintExpr(*s.assignTarget);
                Dedent();
                Line("Value");
                Indent();
                PrintExpr(*s.assignValue);
                Dedent();
                Dedent();
                break;
            case StmtKind::If:
                Line("If");
                Indent();
                Line("Condition");
                Indent();
                PrintExpr(*s.condition);
                Dedent();
                Line("Then");
                Indent();
                PrintStmt(*s.thenBranch);
                Dedent();
                if (s.elseBranch != nullptr) {
                    Line("Else");
                    Indent();
                    PrintStmt(*s.elseBranch);
                    Dedent();
                }
                Dedent();
                break;
            case StmtKind::While:
                Line("While");
                Indent();
                Line("Condition");
                Indent();
                PrintExpr(*s.condition);
                Dedent();
                Line("Body");
                Indent();
                PrintStmt(*s.body);
                Dedent();
                Dedent();
                break;
            case StmtKind::For:
                Line("For");
                Indent();
                if (s.forInit != nullptr) {
                    Line("Init");
                    Indent();
                    PrintStmt(*s.forInit);
                    Dedent();
                }
                Line("Condition");
                Indent();
                PrintExpr(*s.forCond);
                Dedent();
                if (s.forStep != nullptr) {
                    Line("Step");
                    Indent();
                    PrintStmt(*s.forStep);
                    Dedent();
                }
                Line("Body");
                Indent();
                PrintStmt(*s.body);
                Dedent();
                Dedent();
                break;
            case StmtKind::Break:
                Line("Break");
                break;
            case StmtKind::Continue:
                Line("Continue");
                break;
            case StmtKind::Return:
                Line("Return");
                if (s.returnValue != nullptr) {
                    Indent();
                    PrintExpr(*s.returnValue);
                    Dedent();
                }
                break;
            case StmtKind::ExprStmt:
                Line("ExprStmt");
                Indent();
                PrintExpr(*s.expr);
                Dedent();
                break;
        }
    }

    void PrintExpr(const Expr& e) {
        switch (e.kind) {
            case ExprKind::IntLiteral:
                Line("IntLiteral " + std::to_string(e.intValue));
                break;
            case ExprKind::FloatLiteral:
                Line("FloatLiteral " + std::to_string(e.floatValue));
                break;
            case ExprKind::BoolLiteral:
                Line(std::string("BoolLiteral ") + (e.boolValue ? "true" : "false"));
                break;
            case ExprKind::StringLiteral:
                Line("StringLiteral \"" + e.text + "\"");
                break;
            case ExprKind::Identifier:
                Line("Identifier " + e.text);
                break;
            case ExprKind::Binary:
                Line(std::string("Binary ") + ToString(e.binaryOp));
                Indent();
                PrintExpr(*e.lhs);
                PrintExpr(*e.rhs);
                Dedent();
                break;
            case ExprKind::Unary:
                Line(std::string("Unary ") + ToString(e.unaryOp));
                Indent();
                PrintExpr(*e.lhs);
                Dedent();
                break;
            case ExprKind::Call:
                Line("Call");
                Indent();
                PrintExpr(*e.lhs);
                for (const Expr* arg : e.args) {
                    PrintExpr(*arg);
                }
                Dedent();
                break;
            case ExprKind::Member:
                Line("Member ." + e.text);
                Indent();
                PrintExpr(*e.lhs);
                Dedent();
                break;
        }
    }

    void Line(const std::string& text) { out_ << std::string(static_cast<std::size_t>(depth_) * 2, ' ') << text << "\n"; }
    void Indent() { ++depth_; }
    void Dedent() { --depth_; }

    std::ostream& out_;
    int depth_ = 0;
};

} // namespace

void PrintAst(const Program& program, std::ostream& out) {
    Printer(out).PrintProgram(program);
}

} // namespace ce::lang
