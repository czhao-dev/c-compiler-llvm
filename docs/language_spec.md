# MiniC Language Spec

This file documents the grammar implemented by the lexer (`src/lexer.cpp`)
and the recursive-descent parser (`src/parser.cpp`), together with the type
rules enforced by the semantic analyzer (`src/sema.cpp`).

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

### Concrete Examples

**program** — a complete MiniC source file is a sequence of function
definitions. There is no top-level statement syntax; all code lives inside
functions.

```c
int square(int n) { return n * n; }

int main() {
    printf("%d\n", square(7));
    return 0;
}
```

**function** — a return type, a name, a parenthesised parameter list, and
a braced body block.

```c
int add(int a, int b) { return a + b; }
void greet(char c) { printf("%c\n", c); }
```

**params / param** — a comma-separated list of `type name` pairs. The
parameter list may be empty.

```c
// two params
int clamp(int val, int lo, int hi) { ... }

// no params
int zero() { return 0; }
```

**block** — a brace-delimited sequence of zero or more statements. Every
`if`/`else`/`while`/`for` body is a block, even when it contains a single
statement.

```c
{
    int x = 5;
    x = x + 1;
    return x;
}
```

**var_decl** — declares a local variable in the current scope. The
initializer is required for `float` when a value is needed immediately; it
is optional for all types. A declaration without an initializer leaves the
variable uninitialized — the static analyzer will warn if it is read before
being assigned.

```c
int count = 0;
float ratio = 3.14;
char c;            // uninitialized; safe only if assigned before first read
```

**assign_stmt** — assigns a new value to an already-declared variable.
The variable must be in scope; the right-hand side is evaluated first.

```c
x = x + 1;
total = total + fi * fi;
```

**call_stmt** — a function call used as a statement; its return value is
discarded. Used primarily for `printf` and other void-returning functions.

```c
printf("%d\n", fibonacci(i));
```

**if_stmt** — the condition is parenthesised; each branch is a block. The
`else` clause is optional and may be followed by another `if` for
`else if` chains.

```c
if (n <= 1) {
    return n;
}

if (x > 0) {
    printf("positive\n");
} else if (x < 0) {
    printf("negative\n");
} else {
    printf("zero\n");
}
```

**while_stmt** — evaluates the condition before each iteration; exits when
the condition is zero.

```c
while (i < 10) {
    printf("%d\n", i);
    i = i + 1;
}
```

**for_stmt** — the initializer runs once before the first iteration; the
update runs at the end of each iteration. All three clauses are optional
(an omitted condition is treated as always-true).

```c
for (int i = 0; i < n; i = i + 1) {
    total = total + i;
}
```

**return_stmt** — exits the current function, optionally with a value. A
`void` function uses `return;`; a non-`void` function must supply an
expression matching the declared return type.

```c
return n;
return fibonacci(n - 1) + fibonacci(n - 2);
return;    // valid only in a void function
```

**break_stmt / continue_stmt** — `break` exits the innermost enclosing
loop; `continue` jumps to the loop condition (for `while`) or the update
clause (for `for`). Both are errors outside a loop.

```c
while (1) {
    if (done) { break; }
    if (skip) { continue; }
    process();
}
```

---

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

---

## Type System

### Base Types

| Type    | Width  | Notes |
|---------|--------|-------|
| `int`   | 32-bit | signed integer; LLVM `i32` |
| `float` | 32-bit | IEEE 754 single; LLVM `float` |
| `char`  | 8-bit  | signed; LLVM `i8` |
| `void`  | —      | valid only as a function return type |

### Arithmetic Operators (+, -, *, /)

Both operands must be numeric (`int`, `float`, or `char`). The result type
follows C's usual arithmetic conversions:

- If either operand is `float`, the other is implicitly widened to `float`
  and the result is `float`.
- Otherwise the result is `int` (a `char` operand is sign-extended to `int`
  before the operation).

Integer division truncates toward zero (C semantics); there is no implicit
integer-to-float promotion for `/`.

```c
int a = 7 / 2;      // 3 — integer division
float b = 7.0 / 2;  // 3.5 — float division (2 is widened)
```

### Comparison Operators (==, !=, <, >, <=, >=)

Both operands must be numeric. The result is always `int`: `1` if the
comparison holds, `0` otherwise. This matches C semantics and means
comparisons are composable with arithmetic.

### Logical Operators (&&, ||, !)

Operands must be numeric; any non-zero value is treated as true. The result
is `int` (1 or 0). Short-circuit evaluation is **not** guaranteed in the
current implementation — both operands are evaluated.

### Implicit Conversions

| Context | Allowed | Diagnostic |
|---------|---------|-----------|
| `int` ← `float` | yes, truncation | warning: narrowing conversion |
| `char` ← `int` | yes, truncation | warning: narrowing conversion |
| `char` ← `float` | yes, double truncation | warning: narrowing conversion |
| `int` ← `char` | yes, sign-extends | no warning |
| `float` ← `int` | yes, widens | no warning |
| any ← `void` | no | error |

### Variable Declaration and Assignment

The declared type is the *target* type. The initializer's type must be
assignable to the target (see the table above). The same rule applies to
`assign_stmt`.

```c
int x = 3.7;    // warning: narrowing float → int; x = 3
float y = 5;    // ok: int 5 widens to float 5.0
```

### Function Calls

Argument count must exactly match the parameter count (except for the
built-in `printf`, which is variadic). Each argument's type must be
assignable to the corresponding parameter type under the same rules as
variable assignment.

### Return Statements

The expression type must be assignable to the enclosing function's declared
return type. A `void` function may use a bare `return;` or fall off the
end of its body. A non-`void` function that reaches the end of its body
without a `return` triggers a static-analyzer warning (missing return on a
non-void path).

### Condition Expressions

The condition of `if`, `while`, and `for` must be a numeric type (`int`,
`float`, or `char`). A zero value is false; any other value is true. There
is no boolean type.

```c
if (3.14) { ... }   // ok: non-zero float is true
while (n) { ... }   // ok: exits when n == 0
```

---

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

---

## Error Reporting

Syntax errors are thrown immediately with a `file:line:col: error: <message>
(got <token>)` format:

```
$ echo 'int main() { return 0 }' > bad.mc && ./build/minic bad.mc --emit-ast
bad.mc:1:23: error: expected ';' after return statement (got TOK_RBRACE '}')
```

Semantic errors and warnings are collected in a single pass and printed
together after parsing completes, so a single run surfaces all problems:

```
$ ./build/minic bad.mc
bad.mc:3:12: error: use of undeclared variable 'nn'
bad.mc:8:12: warning: narrowing conversion from 'float' to 'int'
bad.mc:15:5: error: return type mismatch — expected 'int', got 'void'
```
