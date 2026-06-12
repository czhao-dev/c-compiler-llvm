# MiniC Language Spec

This file will grow with the parser and semantic analyzer. The current
starter build supports lexing the language features listed in
`MiniC_README.md`, including string literals for `printf`.

## Initial Grammar Sketch

```bnf
program      ::= function*
function     ::= type identifier "(" params? ")" block
params       ::= param ("," param)*
param        ::= type identifier
block        ::= "{" statement* "}"
statement    ::= declaration
               | assignment
               | if_statement
               | while_statement
               | for_statement
               | return_statement
               | break_statement
               | continue_statement
               | expression ";"
```
