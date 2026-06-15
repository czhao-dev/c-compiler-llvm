#include "ast.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

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

    minic::SemanticAnalyzer analyzer;
    return analyzer.analyze(program);
}

std::vector<minic::Diagnostic> analyzeFile(const std::string &path) {
    return analyzeSource(readFile(path), path);
}

bool hasError(const std::vector<minic::Diagnostic> &diags, const std::string &substring) {
    for (const auto &diag : diags) {
        if (diag.severity == minic::DiagnosticSeverity::Error &&
            diag.message.find(substring) != std::string::npos) {
            return true;
        }
    }
    return false;
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

int errorCount(const std::vector<minic::Diagnostic> &diags) {
    int count = 0;
    for (const auto &diag : diags) {
        count += diag.severity == minic::DiagnosticSeverity::Error ? 1 : 0;
    }
    return count;
}

} // namespace

int main() {
    const std::string examplesDir = MINIC_EXAMPLES_DIR;

    // Every example program is well-typed: zero diagnostics.
    for (const std::string &name : {"fibonacci.mc", "gcd.mc", "fizzbuzz.mc", "sum_of_squares.mc"}) {
        const auto diags = analyzeFile(examplesDir + "/" + name);
        if (!diags.empty()) {
            for (const auto &diag : diags) {
                std::cerr << name << ": " << diag.toString() << '\n';
            }
        }
        assert(diags.empty());
    }

    // Forward references: a function may call another defined later in the file.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    return helper();\n"
            "}\n"
            "\n"
            "int helper() {\n"
            "    return 42;\n"
            "}\n");
        assert(diags.empty());
    }

    // Use of an undeclared variable.
    {
        const auto diags = analyzeSource("int main() {\n    return nn;\n}\n", "bad.mc");
        assert(hasError(diags, "use of undeclared variable 'nn'"));
        assert(diags.front().location.line == 2);
    }

    // Call to an undeclared function.
    {
        const auto diags = analyzeSource("int main() {\n    return foo();\n}\n");
        assert(hasError(diags, "call to undeclared function 'foo'"));
    }

    // Wrong number of arguments to a call.
    {
        const auto diags = analyzeSource(
            "int add(int a, int b) {\n"
            "    return a + b;\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    return add(1, 2, 3);\n"
            "}\n");
        assert(hasError(diags, "wrong number of arguments to 'add' \xe2\x80\x94 expected 2, got 3"));
    }

    // Redeclaration of a variable in the same scope.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int x = 1;\n"
            "    int x = 2;\n"
            "    return x;\n"
            "}\n");
        assert(hasError(diags, "redefinition of 'x'"));
    }

    // A variable declared in a nested block does not leak into the outer scope.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    if (1) {\n"
            "        int y = 5;\n"
            "    }\n"
            "    return y;\n"
            "}\n");
        assert(hasError(diags, "use of undeclared variable 'y'"));
    }

    // A non-void function must return a value.
    {
        const auto diags = analyzeSource(
            "int f() {\n"
            "    return;\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    return f();\n"
            "}\n");
        assert(hasError(diags, "non-void function 'f' must return a value"));
    }

    // A void function must not return a value.
    {
        const auto diags = analyzeSource(
            "void f() {\n"
            "    return 1;\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    f();\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "void function 'f' should not return a value"));
    }

    // Assigning an incompatible (non-numeric) type is an error.
    {
        const auto diags = analyzeSource(
            "void f() {}\n"
            "\n"
            "int main() {\n"
            "    int x = f();\n"
            "    return x;\n"
            "}\n");
        assert(hasError(diags, "cannot convert 'void' to 'int'"));
    }

    // Assigning a float to an int is a narrowing-conversion warning, not an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int x = 1.5;\n"
            "    return x;\n"
            "}\n");
        assert(hasWarning(diags, "implicit conversion from 'float' to 'int' may lose precision"));
        assert(errorCount(diags) == 0);
    }

    // break/continue outside of a loop is an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    break;\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "'break' statement not within a loop"));
    }

    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    continue;\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "'continue' statement not within a loop"));
    }

    // break/continue inside a for-loop are fine.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    for (int i = 0; i < 10; i = i + 1) {\n"
            "        if (i == 5) { continue; }\n"
            "        if (i == 8) { break; }\n"
            "    }\n"
            "    return 0;\n"
            "}\n");
        assert(diags.empty());
    }

    // Redefinition of a function.
    {
        const auto diags = analyzeSource(
            "int f() { return 0; }\n"
            "int f() { return 1; }\n"
            "\n"
            "int main() {\n"
            "    return f();\n"
            "}\n");
        assert(hasError(diags, "redefinition of function 'f'"));
    }

    return 0;
}
