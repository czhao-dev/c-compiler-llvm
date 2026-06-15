#include "analyzer.h"
#include "ast.h"
#include "lexer.h"
#include "parser.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef MINIC_EXAMPLES_DIR
#define MINIC_EXAMPLES_DIR "examples"
#endif

namespace {

std::string readFile(const std::string &path) {
    std::ifstream file(path);
    assert(file && "could not open example file");
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<minic::Diagnostic> analyzeSource(const std::string &source, const std::string &filename = "<input>") {
    minic::Lexer lexer(source, filename);
    minic::Parser parser(lexer.tokenize());
    const minic::ProgramNode program = parser.parseProgram();

    minic::StaticAnalyzer analyzer;
    return analyzer.analyze(program);
}

std::vector<minic::Diagnostic> analyzeFile(const std::string &path) {
    return analyzeSource(readFile(path), path);
}

bool hasWarning(const std::vector<minic::Diagnostic> &diags, const std::string &substring) {
    for (const auto &diag : diags) {
        if (diag.severity == minic::DiagnosticSeverity::Warning &&
            diag.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void expectNoDiagnostics(const std::vector<minic::Diagnostic> &diags, const std::string &name) {
    if (!diags.empty()) {
        for (const auto &diag : diags) {
            std::cerr << name << ": " << diag.toString() << '\n';
        }
    }
    assert(diags.empty());
}

} // namespace

int main() {
    const std::string examplesDir = MINIC_EXAMPLES_DIR;

    // The example programs from earlier phases are clean: zero static-analysis
    // warnings (no dead code, unused variables/functions, missing returns, or
    // constant-zero divisors).
    for (const std::string &name : {"fibonacci.mc", "gcd.mc", "fizzbuzz.mc", "sum_of_squares.mc"}) {
        expectNoDiagnostics(analyzeFile(examplesDir + "/" + name), name);
    }

    // examples/warnings/uninitialized.mc: `x` is read before it's assigned.
    {
        const auto diags = analyzeFile(examplesDir + "/warnings/uninitialized.mc");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "variable 'x' may be used uninitialized"));
    }

    // examples/warnings/unreachable.mc: the second `return` can never run.
    {
        const auto diags = analyzeFile(examplesDir + "/warnings/unreachable.mc");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "unreachable code"));
    }

    // examples/warnings/unused_var.mc: `temp` is assigned but never read.
    {
        const auto diags = analyzeFile(examplesDir + "/warnings/unused_var.mc");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "variable 'temp' is declared but never used"));
    }

    // examples/warnings/missing_return.mc: the `a <= b` path falls off the end.
    {
        const auto diags = analyzeFile(examplesDir + "/warnings/missing_return.mc");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "control may reach the end of non-void function 'max' without returning a value"));
    }

    // examples/warnings/unused_function.mc: `helper` is never called.
    {
        const auto diags = analyzeFile(examplesDir + "/warnings/unused_function.mc");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "function 'helper' is defined but never called"));
    }

    // examples/warnings/divide_by_zero.mc: `total / 0` always divides by zero.
    {
        const auto diags = analyzeFile(examplesDir + "/warnings/divide_by_zero.mc");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "division by zero"));
    }

    // --- Negative cases: situations that must NOT trigger a warning. ---

    // A parameter is considered initialized from function entry, so reading
    // it right away is not flagged as a possibly-uninitialized read.
    expectNoDiagnostics(analyzeSource(
                             "int identity(int x) {\n"
                             "    return x;\n"
                             "}\n"),
                         "param-not-uninitialized");

    // If every branch of an if/else returns a value, control cannot fall off
    // the end of the function, even though there's no trailing `return`.
    expectNoDiagnostics(analyzeSource(
                             "int abs(int x) {\n"
                             "    if (x < 0) {\n"
                             "        return -x;\n"
                             "    } else {\n"
                             "        return x;\n"
                             "    }\n"
                             "}\n"),
                         "if-else-both-return");

    // Division by a parameter (not a known constant) is not flagged: its
    // value could be anything at runtime.
    expectNoDiagnostics(analyzeSource(
                             "int divide(int a, int b) {\n"
                             "    return a / b;\n"
                             "}\n"),
                         "division-by-parameter");

    // Call-graph reachability is transitive: helper2 is only called from
    // helper1, which is only called from main, but both count as used.
    expectNoDiagnostics(analyzeSource(
                             "int helper2(int x) {\n"
                             "    return x;\n"
                             "}\n"
                             "\n"
                             "int helper1(int x) {\n"
                             "    return helper2(x);\n"
                             "}\n"
                             "\n"
                             "int main() {\n"
                             "    return helper1(5);\n"
                             "}\n"),
                         "transitive-call-graph");

    // Division-by-zero diagnostics name the expression when the zero divisor
    // is a variable rather than a literal `0`.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int a = 5;\n"
            "    int b = 5;\n"
            "    int c = a - b;\n"
            "    int d = 10 / c;\n"
            "    return d;\n"
            "}\n");
        assert(diags.size() == 1);
        assert(hasWarning(diags, "division by zero ('c' is always 0 here)"));
    }

    return 0;
}
