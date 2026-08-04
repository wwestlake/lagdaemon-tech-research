# 05 - EBNF Grammar & Tokenizer

## Tokens & Lexical Rules

* **Keywords**: `mod`, `use`, `pub`, `as`, `let`, `in`, `fn`, `match`, `with`, `if`, `then`, `else`, `type`, `interface`, `component`, `in`, `out`, `where`, `perform`, `handle`
* **Operators & Delimiters**: `::`, `->`, `=>`, `~>`, `=`, `==`, `!=`, `<`, `<=`, `>`, `>=`, `+`, `-`, `*`, `/`, `%`

---

## EBNF Syntactic Grammar

```ebnf
Program ::= { TopLevelDecl } ;

TopLevelDecl ::= InterfaceDecl | ComponentDecl | FunctionDecl | ModDecl | UseDecl ;

InterfaceDecl ::= "interface" CONSTR_IDENT "{" { PortDecl } [ "where" "{" { ContractExpr } "}" ] "}" ;
PortDecl      ::= ( "in" | "out" ) IDENT ":" TypeSpec ;

ComponentDecl ::= "component" CONSTR_IDENT "(" [ ParamList ] ")" ":" CONSTR_IDENT "{" { ComponentBodyItem } "}" ;

ModDecl ::= "mod" IDENT ( ";" | "{" { TopLevelDecl } "}" ) ;
UseDecl ::= "use" Path [ "as" IDENT ] ";" ;

Path ::= IDENT { "::" ( IDENT | CONSTR_IDENT | "*" ) } ;
```
