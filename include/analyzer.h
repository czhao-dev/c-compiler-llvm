#pragma once

#include "ast.h"
#include "cfg.h"
#include "sema.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace minic {

// Builds a per-function control-flow graph and runs dataflow analyses over
// it to find likely bugs. All diagnostics produced here are warnings: the
// static analyzer never blocks compilation, matching the behavior of
// production linters (clang-tidy, cppcheck).
//
// Checks implemented:
//  - reaching definitions    -> "may be used uninitialized"
//  - CFG reachability        -> unreachable (dead) code
//  - liveness                -> "declared but never used"
//  - call-graph reachability -> "defined but never called"
//  - CFG exit predecessors   -> missing return on a non-void path
//  - constant propagation    -> constant-divisor division by zero
class StaticAnalyzer {
public:
    std::vector<Diagnostic> analyze(const ProgramNode &program);

private:
    void analyzeFunction(const FuncDefNode &func, const CFG &cfg);

    void checkUnreachable(const CFG &cfg);
    void checkUninitialized(const FuncDefNode &func, const CFG &cfg);
    void checkUnusedVariables(const CFG &cfg);
    void checkMissingReturn(const FuncDefNode &func, const CFG &cfg);
    void checkDivisionByZero(const FuncDefNode &func, const CFG &cfg);
    void checkUnusedFunctions(const ProgramNode &program,
                               const std::unordered_map<std::string, CFG> &cfgs);

    void warning(SourceLocation location, std::string message);

    std::vector<Diagnostic> diagnostics_;
};

std::string staticAnalyzerStatus();

} // namespace minic
