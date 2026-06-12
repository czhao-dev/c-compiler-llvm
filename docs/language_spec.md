# MiniC Language Spec

This file documents the grammar implemented by the lexer (`src/lexer.cpp`)
and the recursive-descent parser (`src/parser.cpp`). The semantic analyzer
will add type rules on top of this grammar in a later phase.

## Grammar

```bnf
program      ::= function*
function     ::= type identifier "(" params? ")" block
params       ::= param ("," param)*
param        ::= type identifier

block        ::= "{" statement* "}"

statement    ::= var_decl
               | assign_stmt
               | call_stmt
               | if_stmt
               | while_stmt
               | for_stmt
               | return_stmt
               | break_stmt
               | continue_stmt

var_decl     ::= type identifier ("=" expression)? ";"
assign_stmt  ::= identifier "=" expression ";"
call_stmt    ::= identifier "(" args? ")" ";"

if_stmt      ::= "if" "(" expression ")" block ("else" (if_stmt | block))?
while_stmt   ::= "while" "(" expression ")" block
for_stmt     ::= "for" "(" for_init? ";" expression? ";" assign_stmt_no_semi? ")" block
for_init     ::= var_decl_no_semi | assign_stmt_no_semi

return_stmt  ::= "return" expression? ";"
break_stmt   ::= "break" ";"
continue_stmt ::= "continue" ";"

type         ::= "int" | "float" | "char" | "void"
```

## Expressions

Operator precedence, lowest to highest:

```bnf
expression   ::= logical_or
logical_or   ::= logical_and ("||" logical_and)*
logical_and  ::= equality ("&&" equality)*
equality     ::= comparison (("==" | "!=") comparison)*
comparison   ::= additive (("<" | ">" | "<=" | ">=") additive)*
additive     ::= multiplicative (("+" | "-") multiplicative)*
multiplicative ::= unary (("*" | "/") unary)*
unary        ::= ("!" | "-") unary | primary
primary      ::= int_lit | float_lit | char_lit | string_lit
               | identifier
               | identifier "(" args? ")"
               | "(" expression ")"
args         ::= expression ("," expression)*
```

`int <= 1`, `n - 1`, `a && b`, `!done`, and `fibonacci(n - 1)` are all
examples of expressions covered by this grammar. String literals only
appear as call arguments (e.g., the format string passed to `printf`).

## AST

`include/ast.h` defines one node type per grammar construct:

- `ProgramNode`, `FuncDefNode`, `ParamNode` — top-level structure
- `BlockStmtNode`, `VarDeclStmtNode`, `AssignStmtNode`, `ExprStmtNode`,
  `IfStmtNode`, `WhileStmtNode`, `ForStmtNode`, `ReturnStmtNode`,
  `BreakStmtNode`, `ContinueStmtNode` — statements
- `BinOpExprNode`, `UnaryOpExprNode`, `CallExprNode`, `IdentExprNode`,
  `IntLitExprNode`, `FloatLitExprNode`, `CharLitExprNode`,
  `StringLitExprNode` — expressions

Every node implements `print(std::ostream&, int indent)`, used by the
`--emit-ast` CLI flag to dump an indented tree, e.g.:

```
$ ./build/minic examples/fibonacci.mc --emit-ast
Program
  FuncDef fibonacci -> int
    Param int n
    Block
      If
        Cond
          BinOp <=
            Ident n
            IntLit 1
        Then
          Block
            Return
              Ident n
      Return
        BinOp +
          Call fibonacci
            BinOp -
              Ident n
              IntLit 1
          Call fibonacci
            BinOp -
              Ident n
              IntLit 2
  ...
```

## Error Reporting

Syntax errors throw with a `file:line:col: error: <message> (got <token>)`
format, matching the lexer's error style:

```
$ echo 'int main() { return 0 }' > bad.mc && ./build/minic bad.mc --emit-ast
bad.mc:1:23: error: expected ';' after return statement (got TOK_RBRACE '}')
```
