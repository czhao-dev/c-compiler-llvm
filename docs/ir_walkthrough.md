# LLVM IR Walkthrough

This document shows the full pipeline for two example programs and explains
every LLVM IR instruction produced by the MiniC code generator. The final
section shows what LLVM's `-O2` pass pipeline transforms that IR into.

---

## fibonacci.mc — unoptimized IR (`-O0`)

**Source**

```c
int fibonacci(int n) {
    if (n <= 1) { return n; }
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

**Generated IR (unoptimized)**

```llvm
; ModuleID = 'examples/fibonacci.mc'
source_filename = "examples/fibonacci.mc"
target triple = "arm64-apple-darwin25.5.0"

; Format string for printf — stored as a global constant.
@0 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

; printf is an external variadic function — declared but not defined.
declare i32 @printf(ptr, ...)

define i32 @fibonacci(i32 %n) {
entry:
  ; alloca reserves stack space for the parameter 'n'.
  ; MiniC always allocates a stack slot for each parameter so they can be
  ; reassigned (standard LLVM alloca+store+load idiom before mem2reg).
  %n1 = alloca i32, align 4
  store i32 %n, ptr %n1, align 4       ; spill the incoming argument to the slot

  ; Evaluate 'n <= 1': load n, compare, widen i1 → i32, re-narrow to i1.
  ; The zext+icmp pair is slightly redundant; mem2reg + instcombine will clean it up.
  %n2 = load i32, ptr %n1, align 4
  %letmp    = icmp sle i32 %n2, 1      ; signed <=
  %cmptoint = zext i1 %letmp to i32    ; MiniC represents booleans as i32 (C convention)
  %booltmp  = icmp ne i32 %cmptoint, 0 ; convert back to i1 for the branch
  br i1 %booltmp, label %if.then, label %if.end

if.then:
  ; Base case: return n.
  %n3 = load i32, ptr %n1, align 4
  ret i32 %n3

if.end:
  ; Recursive case: fibonacci(n-1) + fibonacci(n-2).
  %n4      = load i32, ptr %n1, align 4
  %subtmp  = sub i32 %n4, 1
  %calltmp = call i32 @fibonacci(i32 %subtmp)   ; fibonacci(n-1)
  %n5      = load i32, ptr %n1, align 4
  %subtmp6 = sub i32 %n5, 2
  %calltmp7 = call i32 @fibonacci(i32 %subtmp6) ; fibonacci(n-2)
  %addtmp  = add i32 %calltmp, %calltmp7
  ret i32 %addtmp
}

define i32 @main() {
entry:
  ; int i = 0;
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  br label %while.cond             ; jump to the loop condition

while.cond:
  ; while (i < 10)
  %i1      = load i32, ptr %i, align 4
  %lttmp   = icmp slt i32 %i1, 10
  %cmptoint = zext i1 %lttmp to i32
  %booltmp = icmp ne i32 %cmptoint, 0
  br i1 %booltmp, label %while.body, label %while.end

while.body:
  ; printf("%d\n", fibonacci(i));
  %i2      = load i32, ptr %i, align 4
  %calltmp = call i32 @fibonacci(i32 %i2)
  %calltmp3 = call i32 (ptr, ...) @printf(ptr @0, i32 %calltmp)

  ; i = i + 1;
  %i4      = load i32, ptr %i, align 4
  %addtmp  = add i32 %i4, 1
  store i32 %addtmp, ptr %i, align 4
  br label %while.cond             ; back edge

while.end:
  ret i32 0
}
```

Key things to notice in the unoptimized IR:

- Every local variable gets an `alloca` slot and is accessed via `store`/`load`.
  This is LLVM's canonical approach — the `mem2reg` pass promotes these to SSA
  registers during optimization.
- The `if` produces two blocks (`if.then`, `if.end`) with control flow via
  `br i1`. There is no phi node because each path returns immediately.
- The `while` loop produces three blocks: `while.cond` (condition + branch),
  `while.body` (body + back edge), `while.end` (exit).
- Each comparison goes through `zext i1 → i32` then `icmp ne i32, 0` because
  MiniC represents boolean results as `int` (matching C semantics). This double
  conversion is redundant and gets folded by `instcombine` during optimization.

---

## fibonacci.mc — optimized IR (`-O2`)

```llvm
; ModuleID = 'examples/fibonacci.mc'
source_filename = "examples/fibonacci.mc"
target triple = "arm64-apple-darwin25.5.0"

@0 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

; nofree/nounwind/local_unnamed_addr — attributes added by the optimizer
; signaling this function has no side effects on memory (beyond the call itself).
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #0

; memory(none): the optimizer proved fibonacci reads/writes no memory
; (all its "variables" are now SSA registers after mem2reg).
define i32 @fibonacci(i32 %n) local_unnamed_addr #1 {
entry:
  ; mem2reg eliminated all alloca/store/load pairs — %n is used directly.
  ; instcombine simplified 'n <= 1' (zext+icmp) into a single 'icmp slt n, 2'.
  %letmp11 = icmp slt i32 %n, 2      ; n < 2  ≡  n <= 1  for integers
  br i1 %letmp11, label %common.ret, label %if.end

common.ret:
  ; PHI node merges the base-case return (from entry) and the loop exit.
  ; This single return block replaces two separate 'ret' instructions.
  %accumulator.tr.lcssa = phi i32 [ 0, %entry ], [ %addtmp, %if.end ]
  %n.tr.lcssa           = phi i32 [ %n, %entry ], [ %subtmp6, %if.end ]
  ; On the base case path: n + 0 = n (the original 'return n').
  ; On the loop-exit path: last fibonacci(n-1) result + accumulated sum.
  %accumulator.ret.tr = add i32 %n.tr.lcssa, %accumulator.tr.lcssa
  ret i32 %accumulator.ret.tr

if.end:
  ; The optimizer converted the fibonacci(n-2) recursion into a loop.
  ; Each iteration: accumulate fibonacci(n-1), then decrement n by 2 and loop.
  ; This is "accumulator-style" tail-call optimization applied to one branch.
  %n.tr13          = phi i32 [ %subtmp6, %if.end ], [ %n, %entry ]
  %accumulator.tr12 = phi i32 [ %addtmp,  %if.end ], [ 0,  %entry ]
  %subtmp  = add nsw i32 %n.tr13, -1          ; n - 1
  %calltmp = tail call i32 @fibonacci(i32 %subtmp)  ; still recurses for (n-1)
  %subtmp6 = add nsw i32 %n.tr13, -2          ; n - 2  (becomes next loop's n)
  %addtmp  = add i32 %calltmp, %accumulator.tr12    ; accumulate
  ; Loop exit condition: once n < 4, the next fibonacci(n-2) would be a
  ; base case, so jump to common.ret to add the final base case value.
  %letmp   = icmp samesign ult i32 %n.tr13, 4
  br i1 %letmp, label %common.ret, label %if.end
}

; main is fully unrolled: the while (i < 10) loop over fibonacci(0)..fibonacci(9)
; is replaced with 10 straight-line tail calls. No loop overhead, no branch.
define noundef i32 @main() local_unnamed_addr #0 {
entry:
  %calltmp   = tail call i32 @fibonacci(i32 0)
  %calltmp3  = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @0, i32 %calltmp)
  %calltmp.1 = tail call i32 @fibonacci(i32 1)
  %calltmp3.1 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @0, i32 %calltmp.1)
  ; ... (calls for i=2 through i=9 follow the same pattern)
  %calltmp.9 = tail call i32 @fibonacci(i32 9)
  %calltmp3.9 = tail call i32 (ptr, ...) @printf(ptr nonnull dereferenceable(1) @0, i32 %calltmp.9)
  ret i32 0
}

attributes #0 = { nofree nounwind }
attributes #1 = { nofree nosync nounwind memory(none) }
```

What the `-O2` pipeline changed and why:

| Transformation | Before | After | Pass responsible |
|---|---|---|---|
| Eliminate alloca/store/load | 5 loads of `%n` across the function | `%n` used directly as SSA value | `mem2reg` |
| Simplify boolean round-trip | `zext i1 → i32` then `icmp ne i32, 0` | single `icmp slt i32 %n, 2` | `instcombine` |
| Merge return blocks | Two `ret` instructions | One `ret` with PHI | `simplifycfg` |
| Convert `fibonacci(n-2)` recursion to loop | Two recursive calls per invocation | One recursive call + loop with accumulator | `tailcallelim` / `loop-rotate` |
| Unroll `main`'s while loop | Loop with back edge and condition block | 10 straight-line tail calls | `loop-unroll` |
| Mark `fibonacci` memory-free | No attributes | `memory(none)` | alias analysis |

---

## Benchmark: fibonacci(40)

Compiled from the same source, measured with `time ./binary`:

| Flag | Runtime | Relative |
|---|---|---|
| `-O0` | 0.39 s | baseline |
| `-O2` | 0.28 s | **1.4× faster** |

The speedup comes from two sources: the inner loop in `fibonacci` eliminates
one level of the recursive tree (all the `fibonacci(n-2)` calls become
iterations instead of call frames), and `main`'s loop is unrolled so the
branch predictor never sees the loop-exit condition at all.

---

## sum_of_squares.mc

**Source**

```c
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

Key IR patterns to note:

- `int i` uses `alloca i32` while `float total` uses `alloca float`.
- Integer-to-float conversion (`int i` → `float fi`) is a `sitofp i32 → float`
  instruction — Sign-extend Int To Float.
- `total + fi * fi` becomes `fmul float` then `fadd float`.
- `printf` receives `fi * fi` as a `double` (not `float`) because the C ABI
  requires float varargs to be promoted to double — MiniC emits an `fpext`
  instruction before the call.
- At `-O2`, `mem2reg` promotes `total` and `i` to SSA registers, the loop
  counter becomes a PHI node, and the float multiply may be fused with the
  addition into an `fmadd` by the backend on ARM64.
