# MiniC Compiler

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![LLVM](https://img.shields.io/badge/LLVM-17%2B-orange.svg)](https://llvm.org)
[![CMake](https://img.shields.io/badge/CMake-3.20%2B-064F8C.svg)](https://cmake.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

> A compiler for a statically-typed subset of C — from hand-written source
> code through a lexer, parser, semantic analyzer, and LLVM IR generator,
> producing native binaries that run without any runtime dependency.

---

## Overview

MiniC takes C source files written in a well-defined subset of the language
and compiles them to native machine code through a complete pipeline: a
hand-written lexer, a recursive-descent parser, a semantic analyzer that
catches type errors and undeclared variables, and an LLVM IR code generator
that produces binaries via the LLVM backend.

Because the source language is a real subset of C — not an invented DSL —
the project needs no explanation of what the language does or why it
exists. Anyone who has written C can read a MiniC program and understand it
immediately.

The project demonstrates how a compiler works end to end: how source text
becomes tokens, how tokens become a structured tree, how that tree is
type-checked, and how it becomes LLVM IR that the backend turns into a
runnable binary.

**All phases are implemented, tested, and cross-validated against clang.**
`minic <file.mc>` compiles straight to a native binary; `minic <file.mc> -O2`
runs LLVM's full optimization pipeline first. See
[docs/ROADMAP.md](docs/ROADMAP.md) for the phase-by-phase build plan and the
language-coverage roadmap, and
[Testing & Validation](#testing--validation) below for what's actually been verified.

**CLI flags:** `--emit-tokens`, `--emit-ast`, `--emit-ir`,
`-O0`/`-O1`/`-O2`/`-O3`, `-o`.

---

## Supported Language Features

MiniC supports a deliberately constrained subset of C. Every feature in
scope is fully supported; anything outside scope is a clear compile error.

**Types**
`int`, `float`, `char`, `void` (for function return types only), pointers to
any of those (`int *`, `float **`, ...), and fixed-size single-dimension
arrays (`int arr[10]`). No structs yet — see
[docs/ROADMAP.md](docs/ROADMAP.md) for the staged plan to add them.

**Variables and pointers**
Local variable declarations with initializers (`int x = 5;`),
assignment statements (`x = x + 1;`), and use of variables in expressions.
Address-of (`&x`) and dereference (`*p`) work as prefix unary operators,
including assignment through a dereferenced pointer (`*p = 5;`). A pointer
may be compared with `==`/`!=` against another pointer of the same type or
against the literal `0` (a null-pointer constant), and used directly as an
`if`/`while` condition (true when non-null).

**Arrays**
A local variable may be declared with a fixed size (`int arr[10];`) and
indexed for reading or writing (`arr[i] = arr[i] + 1;`). An array decays to
a pointer to its first element wherever it's used as a value — the same
`arr[i]` syntax works whether `arr` is a real array or a pointer parameter
that received a decayed array, and an array can be passed directly where a
pointer parameter is expected. Arrays aren't assignable as a whole and have
no literal-initializer syntax; there's no multi-dimensional array or
pointer-to-array type yet.

**Arithmetic operators**
`+`, `-`, `*`, `/` with standard precedence and associativity.
Integer division truncates toward zero, matching C semantics.

**Comparison and logical operators**
`==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`.
All comparisons produce an `int` result (0 or 1), matching C.

**Control flow**
`if`/`else`, `while`, and `for` loops. Nested control flow is fully
supported. `break` and `continue` inside loops.

**Functions**
Function declarations with typed parameters and return types, function
calls, and `return` statements. Recursive functions are supported because
the IR generator handles forward references correctly.

**Built-in I/O**
`printf` is available as a special built-in that maps to the C standard
library `printf` via an `extern` declaration in the generated IR.
This is enough to write programs that produce visible output.

---

## Example MiniC Programs

```c
// examples/fibonacci.mc

int fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    int i = 0;
    while (i < 10) {
        printf("%d\n", fibonacci(i));
        i = i + 1;
    }
    return 0;
}
```

```c
// examples/sum_of_squares.mc

float sum_of_squares(int n) {
    float total = 0.0;
    int i = 1;
    while (i <= n) {
        float fi = i;
        total = total + fi * fi;
        i = i + 1;
    }
    return total;
}

int main() {
    printf("%f\n", sum_of_squares(100));
    return 0;
}
```

---

## Pipeline Architecture

```
Source file (.mc)
        │
        ▼
    Lexer                  reads characters, emits a flat stream of tokens
                           (keywords, identifiers, literals, operators)
        │  token stream
        ▼
    Parser                 recursive descent, consumes token stream,
                           builds an Abstract Syntax Tree (AST)
        │  AST
        ▼
    Semantic Analyzer      walks the AST, checks:
                           - all variables declared before use
                           - types are compatible across assignments
                           - function call argument counts match
                           - return type matches function declaration
        │  typed AST
        ▼
    LLVM IR Generator      walks the typed AST, emits LLVM IR using
                           IRBuilder — one IR instruction per AST node
        │  LLVM IR (.ll)
        ▼
    LLVM Backend           invoke llc or clang to compile IR to a
                           native binary or object file
        │
        ▼
    Native Binary
```

---

## Pipeline Walkthrough — fibonacci(5)

This is an end-to-end trace showing what each stage of the pipeline does.

**Source**
```c
int fibonacci(int n) {
    if (n <= 1) { return n; }
    return fibonacci(n - 1) + fibonacci(n - 2);
}
```

**After lexing** — a flat list of tokens:
```
TOK_INT  TOK_IDENT("fibonacci")  TOK_LPAREN  TOK_INT  TOK_IDENT("n")
TOK_RPAREN  TOK_LBRACE  TOK_IF  TOK_LPAREN  TOK_IDENT("n")
TOK_LEQ  TOK_NUMBER(1)  ...
```

**After parsing** — an AST:
```
FuncDef(fibonacci, [Param(int, n)], int)
  IfStmt
    BinOp(<=, Ident(n), Number(1))
    Return(Ident(n))
  Return
    BinOp(+,
      Call(fibonacci, [BinOp(-, Ident(n), Number(1))]),
      Call(fibonacci, [BinOp(-, Ident(n), Number(2))]))
```

**After semantic analysis** — the AST is unchanged but every node
carries a resolved type. The analyzer confirms `n` is declared as `int`,
the `<=` comparison operands are both `int`, and the function's return type
matches its declaration.

**After IR generation** — LLVM IR (`-O0`):
```llvm
define i32 @fibonacci(i32 %n) {
entry:
  ; alloca+store+load is LLVM's canonical pattern for mutable locals before mem2reg.
  %n1 = alloca i32, align 4
  store i32 %n, ptr %n1, align 4
  %n2 = load i32, ptr %n1, align 4
  %letmp    = icmp sle i32 %n2, 1
  %cmptoint = zext i1 %letmp to i32
  %booltmp  = icmp ne i32 %cmptoint, 0
  br i1 %booltmp, label %if.then, label %if.end

if.then:
  %n3 = load i32, ptr %n1, align 4
  ret i32 %n3

if.end:
  %n4      = load i32, ptr %n1, align 4
  %subtmp  = sub i32 %n4, 1
  %calltmp = call i32 @fibonacci(i32 %subtmp)
  %n5      = load i32, ptr %n1, align 4
  %subtmp6 = sub i32 %n5, 2
  %calltmp7 = call i32 @fibonacci(i32 %subtmp6)
  %addtmp  = add i32 %calltmp, %calltmp7
  ret i32 %addtmp
}
```

The `if.then` / `if.end` block names come from the code generator; every
`if` statement produces exactly two target blocks. At `-O2`, `mem2reg`
eliminates all the `alloca`/`store`/`load` chains, `instcombine` folds the
double-comparison into a single `icmp slt`, and the optimizer converts the
`fibonacci(n-2)` recursion into a loop (see [docs/ir_walkthrough.md](docs/ir_walkthrough.md)).

**After LLVM backend** — a native binary that runs directly on the CPU.

---

## Testing & Validation

Every pipeline stage has its own test executable, built with nothing more
than `<cassert>` — no external test framework, so the build stays hermetic
and `ctest` is the only thing a contributor needs to run. All 5 suites pass:

```
$ ctest --test-dir build
    Start 1: lexer_test
1/5 Test #1: lexer_test .......................   Passed
    Start 2: smoke_test
2/5 Test #2: smoke_test .......................   Passed
    Start 3: parser_test
3/5 Test #3: parser_test ......................   Passed
    Start 4: sema_test
4/5 Test #4: sema_test ........................   Passed
    Start 5: codegen_test
5/5 Test #5: codegen_test .....................   Passed

100% tests passed, 0 tests failed out of 5
Total Test time (real) =   1.5 sec
```

| Suite | What it exercises |
|---|---|
| `lexer_test` | Token stream shape for keywords, operators, literals, escapes, and comments |
| `parser_test` | AST shape for every grammar construct, plus parse-error messages |
| `sema_test` | Every diagnostic the type checker can produce — undeclared identifiers, type mismatches, argument-count mismatches, return-type checks |
| `codegen_test` | Compiles and **runs** all six example programs through the full pipeline, asserting on exact stdout |
| `smoke_test` | End-to-end CLI sanity check |

**Output correctness is cross-validated against clang.** Every example
program is compiled twice — once through `minic`, once through
`clang -x c` on the same `.mc` source — and the two binaries are diffed:

```
fibonacci: IDENTICAL
fizzbuzz:  IDENTICAL
gcd:       IDENTICAL
sum_of_squares: IDENTICAL
pointer_swap: IDENTICAL
array_sum: IDENTICAL
```

### Bug hunt and hardening

A targeted correctness review across the lexer, parser, semantic analyzer,
and code generator turned up — and fixed — four real defects:

| Bug | Root cause | Fix |
|---|---|---|
| Double, misordered diagnostics on bad assignments | `checkAssign` evaluated the RHS before checking whether the LHS was even declared, so `x = y;` with both undeclared printed the `y` error before the (more important) `x` error | Check the assignment target first; only evaluate the RHS afterward |
| Temp `.ll` file leaked on write failure | `compileToNative` only deleted its temp file after a successful `clang` invocation — an `ofstream` open/write failure threw before cleanup ran | Delete the temp file on every throwing path, not just the success path |
| `1.` and `1e5` failed to lex as floats | The lexer only recognized a `.` followed by a digit, with no exponent handling at all | Accept a bare trailing `.` and `[eE][+-]?[0-9]+` exponents in `lexNumber` |
| sema/codegen type mismatch on `-charVar` | Sema typed unary negation on `char` as `char`; codegen sign-extended to `int` before negating and returned `int` — the two stages disagreed about the expression's type | Sema now returns `int` for negation on any non-float operand, matching codegen and C's integer-promotion rule |

Each fix was verified by hand-constructed regression cases before being
folded back into the full test run above — all 5 suites and all 4
example-vs-clang diffs still pass after the fix.

---

## Optimization

Passing `-O1`, `-O2`, or `-O3` runs LLVM's new-pass-manager pipeline
(`PassBuilder::buildPerModuleDefaultPipeline`) over the generated IR before
handing it to the backend. The same optimized IR is shown by `--emit-ir`.

Key transformations applied at `-O2` to `fibonacci.mc`:

| Pass | Effect |
|---|---|
| `mem2reg` | Eliminates all `alloca`/`store`/`load` pairs; locals become SSA registers |
| `instcombine` | Folds `zext i1 → i32; icmp ne, 0` into a single comparison |
| `simplifycfg` | Merges the two `return` blocks into one with a PHI node |
| `tailcallelim` | Converts the `fibonacci(n-2)` branch into a loop with an accumulator |
| `loop-unroll` | Unrolls `main`'s fixed-count while loop into 10 straight-line calls |

**Benchmark — `fibonacci(40)` on Apple M-series:**

| Flag | Runtime |
|---|---|
| `-O0` | 0.39 s |
| `-O2` | 0.28 s (**1.4× faster**) |

See [docs/ir_walkthrough.md](docs/ir_walkthrough.md) for annotated before/after
IR listings explaining each transformation.

---

## Repo Structure

```
minic-compiler/
├── README.md
├── include/
│   ├── lexer.h
│   ├── token.h
│   ├── parser.h
│   ├── ast.h
│   ├── sema.h               ← semantic analyzer
│   └── codegen.h
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── sema.cpp
│   ├── codegen.cpp
│   └── main.cpp             ← CLI: invoke stages, flags for IR dump
├── tests/
│   ├── lexer_test.cpp
│   ├── parser_test.cpp
│   ├── sema_test.cpp        ← error case tests
│   └── codegen_test.cpp     ← compare output against a clang baseline
├── examples/
│   ├── fibonacci.mc
│   ├── sum_of_squares.mc
│   ├── fizzbuzz.mc
│   ├── gcd.mc
│   ├── pointer_swap.mc
│   └── array_sum.mc
└── docs/
    ├── ROADMAP.md           ← build plan + language-coverage roadmap
    ├── language_spec.md     ← BNF grammar + type rules
    └── ir_walkthrough.md    ← annotated IR for each example program
```

---

## Build & Run

**Dependencies:** LLVM 17+, CMake 3.20+, a C++17 compiler.

```bash
# macOS
brew install llvm cmake

# Ubuntu
sudo apt install llvm cmake
```

### Configure & build

Homebrew LLVM is installed locally at `/opt/homebrew/opt/llvm`. If
`llvm-config` is not on PATH, `scripts/configure.sh` still uses the
Homebrew path automatically.

```bash
./scripts/configure.sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Manual configure command:

```bash
cmake -S . -B build -G Ninja -DLLVM_DIR="$(/opt/homebrew/opt/llvm/bin/llvm-config --cmakedir)"
```

### CLI usage

```bash
# Dump the token stream (lexer output)
./build/minic examples/fibonacci.mc --emit-tokens

# Dump the AST (parser output)
./minic examples/fibonacci.mc --emit-ast

# Compile to a native binary
./minic examples/gcd.mc -o gcd

# Dump LLVM IR (no optimization)
./minic examples/fibonacci.mc --emit-ir

# Dump LLVM IR after the -O2 pass pipeline
./minic examples/fibonacci.mc -O2 --emit-ir

# Compile without optimization (default)
./minic examples/fibonacci.mc -o fibonacci_o0

# Compile with LLVM -O2 optimization pipeline
./minic examples/fibonacci.mc -O2 -o fibonacci_o2

# Compare output against clang for correctness
clang -x c examples/fibonacci.mc -o fibonacci_clang
diff <(./fibonacci_o0) <(./fibonacci_clang)
```

---

## Error Messages

A compiler is only as good as its error messages. MiniC reports errors with
the source line number and a clear description:

```
fibonacci.mc:3:12: error: use of undeclared variable 'nn'
    if (nn <= 1) {
        ^~
fibonacci.mc:8:5: error: return type mismatch — expected 'int', got 'float'
    return 1.5;
    ^~~~~~
fibonacci.mc:12:20: error: wrong number of arguments to 'fibonacci' —
                    expected 1, got 2
    return fibonacci(n - 1, n - 2) + fibonacci(n - 2);
           ^~~~~~~~~~
```
