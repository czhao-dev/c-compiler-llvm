#include "analyzer.h"

#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace minic {
namespace {

// ---------------------------------------------------------------------------
// Statement / expression helpers shared by the checks below
// ---------------------------------------------------------------------------

// Returns the single expression "read" by a simple statement (the
// initializer, assigned value, the expression of an expression-statement, or
// the return value), or nullptr if the statement reads nothing (e.g. `int x;`
// or `return;`).
const ExprNode *stmtReadExpr(const StmtNode &stmt) {
    if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) return decl->init.get();
    if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) return assign->value.get();
    if (const auto *exprStmt = dynamic_cast<const ExprStmtNode *>(&stmt)) return exprStmt->expr.get();
    if (const auto *ret = dynamic_cast<const ReturnStmtNode *>(&stmt)) return ret->value.get();
    return nullptr;
}

// Returns the name of the variable defined (declared or assigned) by a
// simple statement, or nullptr if the statement defines no variable.
const std::string *stmtDefName(const StmtNode &stmt) {
    if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) return &decl->name;
    if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) return &assign->name;
    return nullptr;
}

// Recursively invokes `fn` on every IdentExprNode (variable read) in `expr`.
template <typename Fn>
void forEachIdent(const ExprNode &expr, const Fn &fn) {
    if (const auto *ident = dynamic_cast<const IdentExprNode *>(&expr)) {
        fn(*ident);
    } else if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        forEachIdent(*unary->operand, fn);
    } else if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
        forEachIdent(*binOp->lhs, fn);
        forEachIdent(*binOp->rhs, fn);
    } else if (const auto *call = dynamic_cast<const CallExprNode *>(&expr)) {
        for (const auto &arg : call->args) {
            forEachIdent(*arg, fn);
        }
    }
}

// Recursively invokes `fn` on every CallExprNode in `expr`, including calls
// nested inside other calls' arguments.
template <typename Fn>
void forEachCall(const ExprNode &expr, const Fn &fn) {
    if (const auto *call = dynamic_cast<const CallExprNode *>(&expr)) {
        fn(*call);
        for (const auto &arg : call->args) {
            forEachCall(*arg, fn);
        }
    } else if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        forEachCall(*unary->operand, fn);
    } else if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
        forEachCall(*binOp->lhs, fn);
        forEachCall(*binOp->rhs, fn);
    }
}

// Recursively invokes `fn` on every BinOpExprNode in `expr`, in post-order
// (operands before the operator that combines them).
template <typename Fn>
void forEachBinOp(const ExprNode &expr, const Fn &fn) {
    if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        forEachBinOp(*unary->operand, fn);
    } else if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
        forEachBinOp(*binOp->lhs, fn);
        forEachBinOp(*binOp->rhs, fn);
        fn(*binOp);
    } else if (const auto *call = dynamic_cast<const CallExprNode *>(&expr)) {
        for (const auto &arg : call->args) {
            forEachBinOp(*arg, fn);
        }
    }
}

// ---------------------------------------------------------------------------
// Bit-set helpers for reaching definitions
// ---------------------------------------------------------------------------

using BitSet = std::vector<bool>;

bool unionInto(BitSet &dst, const BitSet &src) {
    bool changed = false;
    for (std::size_t i = 0; i < dst.size(); ++i) {
        if (src[i] && !dst[i]) {
            dst[i] = true;
            changed = true;
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Constant-value lattice for the division-by-zero check
// ---------------------------------------------------------------------------

enum class ConstState { Unknown, Constant, NotConstant };

// `Unknown` (top) means "no information yet — could still turn out to be any
// single constant". `Constant(v)` means every path seen so far assigns the
// same value `v`. `NotConstant` (bottom) means the value varies across paths
// or is not statically known at all (e.g. a parameter).
struct ConstValue {
    ConstState state = ConstState::Unknown;
    long long value = 0;

    bool operator==(const ConstValue &other) const {
        if (state != other.state) return false;
        return state != ConstState::Constant || value == other.value;
    }
    bool operator!=(const ConstValue &other) const { return !(*this == other); }
};

using ConstEnv = std::map<std::string, ConstValue>;

ConstValue lookupConst(const ConstEnv &env, const std::string &name) {
    auto it = env.find(name);
    return it == env.end() ? ConstValue{} : it->second;
}

// Greatest lower bound: combines the values seen on two incoming paths.
ConstValue meetConst(const ConstValue &a, const ConstValue &b) {
    if (a.state == ConstState::Unknown) return b;
    if (b.state == ConstState::Unknown) return a;
    if (a.state == ConstState::NotConstant || b.state == ConstState::NotConstant) {
        return ConstValue{ConstState::NotConstant, 0};
    }
    if (a.value == b.value) return a;
    return ConstValue{ConstState::NotConstant, 0};
}

ConstEnv meetEnv(const ConstEnv &a, const ConstEnv &b) {
    ConstEnv result;
    for (const auto &[name, value] : a) {
        result[name] = meetConst(value, lookupConst(b, name));
    }
    for (const auto &[name, value] : b) {
        if (result.count(name) == 0) {
            result[name] = value;
        }
    }
    return result;
}

long long foldArith(BinaryOp op, long long lhs, long long rhs) {
    switch (op) {
    case BinaryOp::Add: return lhs + rhs;
    case BinaryOp::Sub: return lhs - rhs;
    case BinaryOp::Mul: return lhs * rhs;
    case BinaryOp::Div: return rhs != 0 ? lhs / rhs : 0;
    default: return 0;
    }
}

// Evaluates `expr` to a constant value under `env`, folding `+ - *` and `/`
// (by a known non-zero divisor) over integer/char literals and variables with
// a known constant value. Anything else (floats, calls, strings, comparisons,
// division by a value that may be zero) is `NotConstant`.
ConstValue evalConst(const ExprNode &expr, const ConstEnv &env) {
    if (const auto *intLit = dynamic_cast<const IntLitExprNode *>(&expr)) {
        return ConstValue{ConstState::Constant, intLit->value};
    }
    if (const auto *charLit = dynamic_cast<const CharLitExprNode *>(&expr)) {
        return ConstValue{ConstState::Constant, static_cast<long long>(charLit->value)};
    }
    if (dynamic_cast<const FloatLitExprNode *>(&expr) || dynamic_cast<const StringLitExprNode *>(&expr) ||
        dynamic_cast<const CallExprNode *>(&expr)) {
        return ConstValue{ConstState::NotConstant, 0};
    }
    if (const auto *ident = dynamic_cast<const IdentExprNode *>(&expr)) {
        return lookupConst(env, ident->name);
    }
    if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        const ConstValue operand = evalConst(*unary->operand, env);
        if (unary->op == UnaryOp::Negate) {
            if (operand.state == ConstState::Constant) {
                return ConstValue{ConstState::Constant, -operand.value};
            }
            return operand;
        }
        return ConstValue{ConstState::NotConstant, 0};
    }
    if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
        switch (binOp->op) {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div: {
            const ConstValue lhs = evalConst(*binOp->lhs, env);
            const ConstValue rhs = evalConst(*binOp->rhs, env);
            if (lhs.state == ConstState::NotConstant || rhs.state == ConstState::NotConstant) {
                return ConstValue{ConstState::NotConstant, 0};
            }
            if (lhs.state == ConstState::Constant && rhs.state == ConstState::Constant) {
                if (binOp->op == BinaryOp::Div && rhs.value == 0) {
                    // Reported separately by checkDivisionByZero; don't fold
                    // through (or hide) a division by zero.
                    return ConstValue{ConstState::NotConstant, 0};
                }
                return ConstValue{ConstState::Constant, foldArith(binOp->op, lhs.value, rhs.value)};
            }
            return ConstValue{};
        }
        default:
            // Comparisons (==, <, ...) and logical operators (&&, ||) produce
            // 0/1, which isn't useful for finding constant-zero divisors.
            return ConstValue{ConstState::NotConstant, 0};
        }
    }
    return ConstValue{ConstState::NotConstant, 0};
}

// Applies the effect of a simple statement to `env` (in place).
void applyConstTransfer(ConstEnv &env, const StmtNode &stmt) {
    if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) {
        env[decl->name] = decl->init ? evalConst(*decl->init, env) : ConstValue{ConstState::NotConstant, 0};
    } else if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) {
        env[assign->name] = evalConst(*assign->value, env);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// StaticAnalyzer
// ---------------------------------------------------------------------------

void StaticAnalyzer::warning(SourceLocation location, std::string message) {
    diagnostics_.push_back(Diagnostic{DiagnosticSeverity::Warning, std::move(location), std::move(message)});
}

std::vector<Diagnostic> StaticAnalyzer::analyze(const ProgramNode &program) {
    diagnostics_.clear();

    std::unordered_map<std::string, CFG> cfgs;
    for (const auto &func : program.functions) {
        CFGBuilder builder;
        cfgs.emplace(func->name, builder.build(*func));
    }

    for (const auto &func : program.functions) {
        analyzeFunction(*func, cfgs.at(func->name));
    }

    checkUnusedFunctions(program, cfgs);

    return std::move(diagnostics_);
}

void StaticAnalyzer::analyzeFunction(const FuncDefNode &func, const CFG &cfg) {
    checkUnreachable(cfg);
    checkUninitialized(func, cfg);
    checkUnusedVariables(cfg);
    checkMissingReturn(func, cfg);
    checkDivisionByZero(func, cfg);
}

// ---------------------------------------------------------------------------
// Unreachable code: reachability traversal from the entry block
// ---------------------------------------------------------------------------

void StaticAnalyzer::checkUnreachable(const CFG &cfg) {
    std::vector<bool> visited(cfg.blocks.size(), false);
    std::vector<int> stack = {cfg.entry};
    visited[cfg.entry] = true;
    while (!stack.empty()) {
        const int b = stack.back();
        stack.pop_back();
        for (int succ : cfg.blocks[b].successors) {
            if (!visited[succ]) {
                visited[succ] = true;
                stack.push_back(succ);
            }
        }
    }

    for (const auto &block : cfg.blocks) {
        if (visited[block.id] || block.id == cfg.exit) {
            continue;
        }
        if (!block.statements.empty()) {
            warning(block.statements.front()->location, "unreachable code");
        } else if (block.condition) {
            warning(block.condition->location, "unreachable code");
        }
    }
}

// ---------------------------------------------------------------------------
// Uninitialized reads: reaching definitions, forward fixpoint
//
// For every local variable `v`, a synthetic "possibly undefined" pseudo-
// definition Undef_v is:
//   - present in IN[entry] for every local variable (except those that
//     shadow a parameter, which start initialized);
//   - generated (GEN) by `T v;` with no initializer;
//   - killed (KILL) by `T v = ...;` and by any assignment to `v`.
//
// OUT[b] = GEN[b] ∪ (IN[b] - KILL[b]), IN[b] = ∪ OUT[pred]. A read of `v` is
// flagged if Undef_v is in the reach set at that point — i.e. some path to
// the read never assigned `v`.
// ---------------------------------------------------------------------------

void StaticAnalyzer::checkUninitialized(const FuncDefNode &func, const CFG &cfg) {
    std::vector<std::string> localVars;
    std::unordered_map<std::string, std::size_t> varIndex;
    for (const auto &block : cfg.blocks) {
        for (const auto *stmt : block.statements) {
            if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(stmt)) {
                if (varIndex.emplace(decl->name, localVars.size()).second) {
                    localVars.push_back(decl->name);
                }
            }
        }
    }
    const std::size_t n = localVars.size();
    if (n == 0) {
        return;
    }

    const std::size_t numBlocks = cfg.blocks.size();
    std::vector<BitSet> gen(numBlocks, BitSet(n, false));
    std::vector<BitSet> kill(numBlocks, BitSet(n, false));

    for (std::size_t b = 0; b < numBlocks; ++b) {
        // For each variable touched in this block, remember whether the
        // *last* touch was a no-initializer declaration (regenerates
        // Undef_v) or a real definition (kills it). Either way the variable
        // is "touched", so its Undef_v is always in KILL[b].
        std::unordered_map<std::size_t, bool> touched;
        for (const auto *stmt : cfg.blocks[b].statements) {
            if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(stmt)) {
                touched[varIndex.at(decl->name)] = (decl->init == nullptr);
                continue;
            }
            if (const std::string *name = stmtDefName(*stmt)) {
                auto it = varIndex.find(*name);
                if (it != varIndex.end()) {
                    touched[it->second] = false;
                }
            }
        }
        for (const auto &[idx, isUndef] : touched) {
            kill[b][idx] = true;
            if (isUndef) {
                gen[b][idx] = true;
            }
        }
    }

    std::vector<BitSet> in(numBlocks, BitSet(n, false));
    std::vector<BitSet> out(numBlocks, BitSet(n, false));

    // Every local variable starts "possibly undefined" at entry, except one
    // that shares a name with a parameter (which starts initialized).
    in[cfg.entry] = BitSet(n, true);
    for (const auto &param : func.params) {
        auto it = varIndex.find(param.name);
        if (it != varIndex.end()) {
            in[cfg.entry][it->second] = false;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t b = 0; b < numBlocks; ++b) {
            if (static_cast<int>(b) != cfg.entry) {
                BitSet newIn(n, false);
                for (int pred : cfg.blocks[b].predecessors) {
                    unionInto(newIn, out[pred]);
                }
                if (newIn != in[b]) {
                    in[b] = std::move(newIn);
                    changed = true;
                }
            }
            BitSet newOut = gen[b];
            for (std::size_t i = 0; i < n; ++i) {
                if (in[b][i] && !kill[b][i]) {
                    newOut[i] = true;
                }
            }
            if (newOut != out[b]) {
                out[b] = std::move(newOut);
                changed = true;
            }
        }
    }

    // Per-statement walk: check each read against the reach set *before*
    // the statement's own definition takes effect, then update the set.
    for (std::size_t b = 0; b < numBlocks; ++b) {
        BitSet reach = in[b];
        auto checkUses = [&](const ExprNode *expr) {
            if (!expr) return;
            forEachIdent(*expr, [&](const IdentExprNode &ident) {
                auto it = varIndex.find(ident.name);
                if (it != varIndex.end() && reach[it->second]) {
                    warning(ident.location, "variable '" + ident.name + "' may be used uninitialized");
                }
            });
        };

        for (const auto *stmt : cfg.blocks[b].statements) {
            checkUses(stmtReadExpr(*stmt));

            if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(stmt)) {
                reach[varIndex.at(decl->name)] = (decl->init == nullptr);
            } else if (const std::string *name = stmtDefName(*stmt)) {
                auto it = varIndex.find(*name);
                if (it != varIndex.end()) {
                    reach[it->second] = false;
                }
            }
        }

        checkUses(cfg.blocks[b].condition);
    }
}

// ---------------------------------------------------------------------------
// Unused variables: liveness, backward fixpoint
//
// USE[b]: variables read in b before any (re)definition within b.
// DEF[b]: variables (re)defined anywhere in b.
// IN[b] = USE[b] ∪ (OUT[b] - DEF[b]); OUT[b] = ∪ IN[succ]; OUT[exit] = {}.
//
// A declaration `T v = ...;` (or `T v;`) is flagged if `v` is not live
// immediately afterwards on any path — i.e. its value is never read before
// being overwritten or going out of scope.
// ---------------------------------------------------------------------------

void StaticAnalyzer::checkUnusedVariables(const CFG &cfg) {
    const std::size_t numBlocks = cfg.blocks.size();

    std::vector<std::set<std::string>> use(numBlocks), def(numBlocks);
    for (std::size_t b = 0; b < numBlocks; ++b) {
        auto addUses = [&](const ExprNode *expr) {
            if (!expr) return;
            forEachIdent(*expr, [&](const IdentExprNode &ident) {
                if (def[b].count(ident.name) == 0) {
                    use[b].insert(ident.name);
                }
            });
        };
        for (const auto *stmt : cfg.blocks[b].statements) {
            addUses(stmtReadExpr(*stmt));
            if (const std::string *name = stmtDefName(*stmt)) {
                def[b].insert(*name);
            }
        }
        addUses(cfg.blocks[b].condition);
    }

    std::vector<std::set<std::string>> in(numBlocks), out(numBlocks);
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t b = 0; b < numBlocks; ++b) {
            std::set<std::string> newOut;
            for (int succ : cfg.blocks[b].successors) {
                newOut.insert(in[succ].begin(), in[succ].end());
            }
            if (newOut != out[b]) {
                out[b] = std::move(newOut);
                changed = true;
            }

            std::set<std::string> newIn = use[b];
            for (const auto &name : out[b]) {
                if (def[b].count(name) == 0) {
                    newIn.insert(name);
                }
            }
            if (newIn != in[b]) {
                in[b] = std::move(newIn);
                changed = true;
            }
        }
    }

    // Per-statement backward walk within each block, seeded by OUT[b] (plus
    // the block's trailing condition, which is read-only).
    for (std::size_t b = 0; b < numBlocks; ++b) {
        std::set<std::string> live = out[b];
        if (cfg.blocks[b].condition) {
            forEachIdent(*cfg.blocks[b].condition, [&](const IdentExprNode &ident) { live.insert(ident.name); });
        }

        const auto &statements = cfg.blocks[b].statements;
        for (auto it = statements.rbegin(); it != statements.rend(); ++it) {
            const StmtNode &stmt = **it;
            if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) {
                if (live.count(decl->name) == 0) {
                    warning(decl->location, "variable '" + decl->name + "' is declared but never used");
                }
                live.erase(decl->name);
                if (decl->init) {
                    forEachIdent(*decl->init, [&](const IdentExprNode &ident) { live.insert(ident.name); });
                }
            } else if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) {
                live.erase(assign->name);
                forEachIdent(*assign->value, [&](const IdentExprNode &ident) { live.insert(ident.name); });
            } else if (const ExprNode *read = stmtReadExpr(stmt)) {
                forEachIdent(*read, [&](const IdentExprNode &ident) { live.insert(ident.name); });
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Missing return: every predecessor of the exit block must end with `return`
// ---------------------------------------------------------------------------

void StaticAnalyzer::checkMissingReturn(const FuncDefNode &func, const CFG &cfg) {
    if (func.returnType == Type::Void) {
        return;
    }
    for (int pred : cfg.blocks[cfg.exit].predecessors) {
        const auto &block = cfg.blocks[pred];
        const bool endsInReturn =
            !block.statements.empty() && dynamic_cast<const ReturnStmtNode *>(block.statements.back());
        if (!endsInReturn) {
            warning(func.location, "control may reach the end of non-void function '" + func.name +
                                        "' without returning a value");
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Constant-divisor division by zero: constant propagation, forward fixpoint
// ---------------------------------------------------------------------------

void StaticAnalyzer::checkDivisionByZero(const FuncDefNode &func, const CFG &cfg) {
    const std::size_t numBlocks = cfg.blocks.size();
    std::vector<ConstEnv> in(numBlocks), out(numBlocks);

    // Parameters are NotConstant from the start: their values aren't known
    // statically. Other local variables default to Unknown (absent).
    for (const auto &param : func.params) {
        in[cfg.entry][param.name] = ConstValue{ConstState::NotConstant, 0};
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t b = 0; b < numBlocks; ++b) {
            if (static_cast<int>(b) != cfg.entry) {
                ConstEnv newIn;
                bool first = true;
                for (int pred : cfg.blocks[b].predecessors) {
                    newIn = first ? out[pred] : meetEnv(newIn, out[pred]);
                    first = false;
                }
                if (newIn != in[b]) {
                    in[b] = std::move(newIn);
                    changed = true;
                }
            }

            ConstEnv newOut = in[b];
            for (const auto *stmt : cfg.blocks[b].statements) {
                applyConstTransfer(newOut, *stmt);
            }
            if (newOut != out[b]) {
                out[b] = std::move(newOut);
                changed = true;
            }
        }
    }

    for (std::size_t b = 0; b < numBlocks; ++b) {
        ConstEnv env = in[b];
        auto check = [&](const ExprNode *expr) {
            if (!expr) return;
            forEachBinOp(*expr, [&](const BinOpExprNode &binOp) {
                if (binOp.op != BinaryOp::Div) {
                    return;
                }
                const ConstValue divisor = evalConst(*binOp.rhs, env);
                if (divisor.state != ConstState::Constant || divisor.value != 0) {
                    return;
                }
                if (dynamic_cast<const IntLitExprNode *>(binOp.rhs.get())) {
                    warning(binOp.rhs->location, "division by zero");
                } else {
                    warning(binOp.rhs->location,
                            "division by zero ('" + exprToString(*binOp.rhs) + "' is always 0 here)");
                }
            });
        };

        for (const auto *stmt : cfg.blocks[b].statements) {
            check(stmtReadExpr(*stmt));
            applyConstTransfer(env, *stmt);
        }
        check(cfg.blocks[b].condition);
    }
}

// ---------------------------------------------------------------------------
// Unused functions: call-graph reachability from `main`
// ---------------------------------------------------------------------------

void StaticAnalyzer::checkUnusedFunctions(const ProgramNode &program,
                                           const std::unordered_map<std::string, CFG> &cfgs) {
    bool hasMain = false;
    std::unordered_set<std::string> definedFunctions;
    for (const auto &func : program.functions) {
        definedFunctions.insert(func->name);
        hasMain = hasMain || func->name == "main";
    }
    if (!hasMain) {
        return;
    }

    std::unordered_map<std::string, std::set<std::string>> callees;
    for (const auto &func : program.functions) {
        std::set<std::string> &calls = callees[func->name];
        const CFG &cfg = cfgs.at(func->name);
        for (const auto &block : cfg.blocks) {
            for (const auto *stmt : block.statements) {
                if (const ExprNode *read = stmtReadExpr(*stmt)) {
                    forEachCall(*read, [&](const CallExprNode &call) { calls.insert(call.callee); });
                }
            }
            if (block.condition) {
                forEachCall(*block.condition, [&](const CallExprNode &call) { calls.insert(call.callee); });
            }
        }
    }

    std::set<std::string> reached = {"main"};
    std::vector<std::string> stack = {"main"};
    while (!stack.empty()) {
        const std::string current = stack.back();
        stack.pop_back();
        for (const auto &callee : callees[current]) {
            if (definedFunctions.count(callee) > 0 && reached.insert(callee).second) {
                stack.push_back(callee);
            }
        }
    }

    for (const auto &func : program.functions) {
        if (reached.count(func->name) == 0) {
            warning(func->location, "function '" + func->name + "' is defined but never called");
        }
    }
}

std::string staticAnalyzerStatus() {
    return "static analyzer: CFG construction and dataflow checks implemented";
}

} // namespace minic
