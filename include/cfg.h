#pragma once

#include "ast.h"

#include <ostream>
#include <string>
#include <vector>

namespace minic {

// A basic block: a maximal run of "simple" statements (variable
// declarations, assignments, expression statements, and returns) with no
// branches in except at the start and no branches out except at the end.
//
// If `condition` is set, control leaves the block via a two-way branch on
// that expression (the first successor is the "true" branch, the second is
// the "false" branch). Otherwise the block falls through to its single
// successor, or has no successor at all (the function's exit block, or an
// unreachable block whose only path out was a `return`/`break`/`continue`
// that was itself unreachable).
struct CFGBlock {
    int id = -1;
    std::vector<const StmtNode *> statements;
    const ExprNode *condition = nullptr;
    std::vector<int> successors;
    std::vector<int> predecessors;
};

// Control-flow graph for a single function.
class CFG {
public:
    std::vector<CFGBlock> blocks;
    int entry = -1;
    int exit = -1;

    int addBlock();
    void addEdge(int from, int to);

    // Prints this CFG as a Graphviz `subgraph cluster_<functionName>` block
    // (without the enclosing `digraph { ... }`), so multiple functions can
    // be combined into a single DOT file.
    void printDot(std::ostream &out, const std::string &functionName) const;
};

// Builds a CFG from a function's AST body.
//
// - A new basic block starts at the function entry, after every branch, and
//   at every branch target.
// - `if` creates edges from the condition block to both the then-block and
//   the else-block (or directly to the merge block when there is no else),
//   and both branches have edges to the merge block.
// - `while`/`for` create a back edge from the end of the loop body (or, for
//   `for`, from the update expression) to the condition block, plus an exit
//   edge from the condition to the block after the loop.
// - `return`, `break`, and `continue` terminate the current block and
//   connect it to the function exit, the loop-exit block, or the
//   loop-continue target (the update block, for `for`) respectively.
class CFGBuilder {
public:
    CFG build(const FuncDefNode &func);

private:
    struct LoopTargets {
        int continueTarget;
        int breakTarget;
    };

    // Builds `block`'s statements into the CFG starting at `current`.
    // Returns the block where control flow continues afterwards, or -1 if
    // every path through `block` already terminated via `return`, `break`,
    // or `continue`.
    int buildBlock(const BlockStmtNode &block, int current);
    int buildStmt(const StmtNode &stmt, int current);
    int buildIf(const IfStmtNode &stmt, int current);
    int buildWhile(const WhileStmtNode &stmt, int current);
    int buildFor(const ForStmtNode &stmt, int current);

    // Joins the (possibly empty) set of blocks where control flow continues
    // after a branch. Returns -1 if every branch terminated, the single
    // surviving block if exactly one branch continues and it isn't
    // `condBlock` itself, or a fresh merge block with edges from every
    // entry otherwise.
    int join(const std::vector<int> &branchEnds, int condBlock);

    CFG cfg_;
    std::vector<LoopTargets> loopStack_;
};

// One-line textual renderings of expressions and simple statements, used for
// CFG labels in `--emit-cfg` output.
std::string exprToString(const ExprNode &expr);
std::string stmtToString(const StmtNode &stmt);

} // namespace minic
