# Static Analysis Design

This document describes the control-flow graph (CFG) that MiniC builds for
each function and the dataflow analyses layered on top of it. It is the
companion to [`include/cfg.h`](../include/cfg.h),
[`src/cfg.cpp`](../src/cfg.cpp), [`include/analyzer.h`](../include/analyzer.h),
and [`src/analyzer.cpp`](../src/analyzer.cpp) — read this first to understand
*why* those files are structured the way they are, then read the source for
the exact implementation.

All six checks below are independent passes over the same per-function CFG
(plus, for the unused-function check, a whole-program call graph built from
those CFGs). Every diagnostic produced by the static analyzer is a
**warning** — it never blocks compilation.

---

## 1. Control-flow graph construction

A `CFG` (see `include/cfg.h`) is a vector of `CFGBlock`s plus a designated
`entry` and `exit` block index. Each block holds:

- `statements` — a run of *simple* statements (`VarDeclStmtNode`,
  `AssignStmtNode`, `ExprStmtNode`, `ReturnStmtNode`) with no internal
  control flow.
- `condition` — set only on a block that ends in an `if`/`while`/`for`
  test; the expression that decides which successor is taken.
- `successors` / `predecessors` — edges to/from other blocks by index.

`CFGBuilder::build` walks a function's body and produces blocks such that:

- A new block starts at function entry, after every branch, and at every
  branch target (loop header, loop exit, if/else merge point).
- `IfStmtNode` ends the current block with `condition` set, and adds edges
  to the then-block and the else-block (or directly to the merge block when
  there is no `else`). Both branches eventually flow to a merge block,
  unless a branch always terminates (e.g. ends in `return`), in which case
  it contributes no edge to the merge.
- `WhileStmtNode`/`ForStmtNode` produce a condition block with two
  successors — the loop body and the after-loop block — and a back edge
  from the end of the body to the condition block (for `for`, via the
  update statement's block).
- `ReturnStmtNode` ends the current block with an edge straight to `exit`;
  the builder returns "no fallthrough" so any statements that follow are
  placed in a fresh block with **no predecessors** — i.e. dead code.
- `BreakStmtNode`/`ContinueStmtNode` end the current block with an edge to
  the enclosing loop's after-block / condition-block respectively, tracked
  via a stack of `LoopTargets`.

`CFG::printDot` renders this structure as a Graphviz `subgraph cluster_<fn>`
(`--emit-cfg`), which is the fastest way to sanity-check the builder visually
before trusting the dataflow passes built on top of it.

Every analysis below is a **fixpoint iteration**: repeatedly recompute
per-block sets from their neighbors until nothing changes. Because each
transfer function is monotone over a finite lattice, this is guaranteed to
terminate.

---

## 2. Uninitialized reads — reaching definitions (forward)

**Goal:** warn when a variable is read on a path that never assigned it a
value.

**Domain:** for each local variable `v` declared anywhere in the function, a
synthetic pseudo-definition `Undef_v` represents "`v` still holds no value".
The analysis tracks, per program point, the set of `Undef_v` pseudo-defs that
*reach* that point. If `Undef_v` reaches a read of `v`, that read may observe
an uninitialized value.

This collapses the textbook "set of reaching definition *sites*" formulation
(which would need a bit per definition site) down to one bit per variable,
because for this check we only care about the single synthetic
"possibly-still-undefined" definition — not which real assignment last wrote
`v`.

**GEN/KILL per block**, computed from the *last* textual touch of each
variable in the block:

- `T v;` (declaration with **no** initializer) → `GEN[b] ∋ Undef_v`,
  `KILL[b] ∋ Undef_v`. (It re-generates the "undefined" pseudo-def and kills
  whatever reached the block before it.)
- `T v = expr;`, or any assignment `v = expr;` → `KILL[b] ∋ Undef_v`,
  not in `GEN[b]`. (The variable now definitely holds a value.)
- If a variable is touched more than once in a block, only the *last* touch
  matters — `GEN`/`KILL` are computed from a single "last touch" map, not
  accumulated.

**Fixpoint equations** (forward, `BitSet` indexed by variable):

```
IN[entry]  = { Undef_v : v is a local variable not shadowing a parameter }
IN[b]      = ⋃ OUT[p]  for p in predecessors(b)     (b ≠ entry)
OUT[b]     = GEN[b] ∪ (IN[b] − KILL[b])
```

A parameter-shadowed local (a declared variable whose name matches a
parameter) starts **out** of `IN[entry]`, since parameters are always
initialized by the caller.

**Reporting:** after the fixpoint, walk each block's statements in order
starting from `reach = IN[b]`. Before applying a statement's own effect,
check every `IdentExprNode` read by that statement (and by the block's
trailing `condition`): if `Undef_v ∈ reach` for that identifier, warn

```
variable 'v' may be used uninitialized
```

Then update `reach` exactly as `GEN`/`KILL` would for that one statement,
so a use *within the same block but after* an initializing assignment is not
flagged (e.g. `int x; x = 5; return x;` is clean).

---

## 3. Unreachable code — reachability (DFS from entry)

**Goal:** warn about code that can never execute.

This is a plain graph reachability problem, not a fixpoint dataflow analysis:
run a DFS/BFS from `cfg.entry` following `successors` edges and mark every
visited block. Any block that is:

- not visited, and
- not the synthetic `exit` block (which legitimately has zero predecessors
  only when *every* path returns before reaching it — handled separately by
  the missing-return check),

is unreachable. Report

```
unreachable code
```

at the location of its first statement, or at its `condition`'s location if
the block is empty except for a trailing condition.

The CFG builder already isolates dead code into its own predecessor-less
block (see §1, `ReturnStmtNode`), so this pass never needs to look inside a
block — only at block-level connectivity.

---

## 4. Unused variables — liveness (backward)

**Goal:** warn when a declared variable's value is never read before it goes
out of scope, is reassigned, or the function returns.

**Domain:** for each block, the set of variable *names* that are live (their
current value may still be read on some path).

**USE/DEF per block**, computed in forward order so that a variable defined
earlier in the block shadows a later "use" of the pre-block value:

- `USE[b]` — names read by a statement's right-hand side or condition,
  *before* that block redefines them.
- `DEF[b]` — names assigned anywhere in the block (`VarDeclStmtNode` or
  `AssignStmtNode` targets).

**Fixpoint equations** (backward, `set<string>`):

```
OUT[b]    = ⋃ IN[s]  for s in successors(b)      (OUT[exit] = ∅)
IN[b]     = USE[b] ∪ (OUT[b] − DEF[b])
```

This is the textbook dual of reaching definitions: same shape, edges and
GEN/KILL roles reversed, fixpoint runs over predecessors↔successors.

**Reporting:** block-level `IN`/`OUT` sets aren't precise enough to catch
"declared, then immediately reassigned without being read in between" within
a single block, so the reporting pass does a second, **per-statement**
backward walk seeded from `OUT[b]` (plus any names read by the block's
trailing `condition`, which are always live going backward into the block):

- Walking statements in reverse, a `VarDeclStmtNode` for `v` is flagged

  ```
  variable 'v' is declared but never used
  ```

  if `v ∉ live` at that point — i.e. no statement between this declaration
  and the end of the block (or, transitively via `OUT[b]`, after the block)
  reads `v` before something else defines it.
- After checking, `live` is updated: `v` is removed (its lifetime starts
  here going backward), and any names read by its initializer are added.
- `AssignStmtNode` similarly removes its target from `live` and adds the
  names read by its RHS. Any other statement just adds the names it reads.

---

## 5. Unused functions — call-graph reachability (BFS from `main`)

**Goal:** warn about functions that are defined but never called from any
function reachable from `main`.

**Construction:** for each function `f`, scan every block of `f`'s CFG (its
statements' read-expressions and trailing conditions) for `CallExprNode`s,
recording the set of callee names `calls(f)`. Built-in functions (`printf`)
are not in `definedFunctions` and are simply ignored as call-graph nodes.

**Reachability:** BFS/DFS over `calls` starting from `{"main"}`. Any defined
function not in the reached set is reported:

```
function 'f' is defined but never called
```

This is the same algorithm as §3 applied to a different graph (functions and
calls instead of blocks and control-flow edges) — deliberately, to reuse the
same mental model.

**Special case:** if the program defines no `main` at all, this check is
skipped entirely — there's no canonical root to reach from, so every helper
function would trivially be "unreached" and the check would be all false
positives.

A function that calls itself (directly or mutually recursively) but is never
called from anything reachable from `main` is still correctly reported as
unused: reachability is computed from `main`, not from "has any caller at
all".

---

## 6. Missing return — exit-predecessor check

**Goal:** for a non-`void` function, warn if control can fall off the end of
the function without executing a `return <value>;`.

No fixpoint needed: after CFG construction, every path from `entry` to `exit`
is represented by *some* predecessor of the `exit` block. A predecessor
"properly" reaches exit only if its last statement is a `ReturnStmtNode`
(`buildStmt` only ever adds a block→exit edge for a block ending in `return`,
**or** for a merge/after block that simply falls through to the end of the
function body).

So: for `func.returnType != Void`, iterate `predecessors(cfg.exit)`. If any
predecessor's statement list is empty or does not end in `ReturnStmtNode`,
warn once:

```
control may reach the end of non-void function 'f' without returning a value
```

at the function's own location, and stop (one diagnostic per function is
enough — this is a binary "every path returns" property, not something that
benefits from per-path detail).

---

## 7. Constant-divisor division by zero — constant propagation (forward)

**Goal:** warn when a `/` operator's divisor is *provably* `0` on every path
that reaches it.

**Lattice** (`ConstValue`, per variable):

```
        Unknown   (⊤ — no information yet)
       /        \
 Constant(c)  ... Constant(c')   (one per integer value)
       \        /
       NotConstant   (⊥ — varies across paths, or statically unknowable)
```

`meet` (`⊓`, used at control-flow merges) is the greatest-lower-bound:
`Unknown ⊓ x = x`; `Constant(c) ⊓ Constant(c) = Constant(c)`;
`Constant(c) ⊓ Constant(c') = NotConstant` for `c ≠ c'`; anything `⊓
NotConstant = NotConstant`. `Unknown` is the identity/top element so that a
variable not yet mentioned on a path doesn't artificially drag everything to
`NotConstant`.

**Initial state:** every function parameter starts `NotConstant` in
`IN[entry]` — its value comes from the caller and is not known statically.
Local variables default to `Unknown` (absent from the map) until their first
definition.

**Transfer function** `applyConstTransfer` (forward, `map<string,
ConstValue>`):

- `T v = expr;` / `v = expr;` → `env[v] = evalConst(expr, env)`.
- `evalConst` folds integer/char literals, unary `-`, and `+ - * /` over
  operands that are both `Constant`. Floats, strings, calls, comparisons,
  and logical operators are `NotConstant` (not useful for finding zero
  divisors). **Division by a `Constant(0)` divisor is deliberately *not*
  folded** — it's left `NotConstant` so the diagnostic below isn't
  accidentally suppressed or hidden behind a folded result.

**Fixpoint equations:**

```
IN[entry] = { p ↦ NotConstant : p is a parameter }
IN[b]     = ⊓ OUT[p]  for p in predecessors(b)     (b ≠ entry)
OUT[b]    = apply each statement's transfer function to IN[b], in order
```

**Reporting:** walk each block's statements in order from `env = IN[b]`.
Before applying a statement's transfer, scan every `BinOpExprNode` with
`op == Div` in its read-expression (and the block's trailing `condition`).
If `evalConst(rhs, env) == Constant(0)`, warn:

- if the divisor is a literal `0`:

  ```
  division by zero
  ```

- otherwise (a variable provably `0` here):

  ```
  division by zero ('<expr>' is always 0 here)
  ```

  where `<expr>` is the divisor expression rendered via `exprToString`.

---

## Summary table

| Check                | Direction | Domain                          | Algorithm                  |
|-----------------------|-----------|----------------------------------|-----------------------------|
| Uninitialized read    | Forward   | bitset of `Undef_v` per variable | Reaching definitions        |
| Unreachable code      | —         | visited/unvisited blocks         | DFS reachability             |
| Unused variable       | Backward  | set of live variable names       | Liveness                      |
| Unused function       | —         | visited/unvisited functions      | BFS reachability (call graph) |
| Missing return        | —         | exit-block predecessors          | Direct CFG inspection         |
| Division by zero      | Forward   | map of variable → `ConstValue`   | Constant propagation          |
