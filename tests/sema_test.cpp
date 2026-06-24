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
    for (const std::string &name : {"fibonacci.mc", "gcd.mc", "fizzbuzz.mc", "sum_of_squares.mc",
                                     "pointer_swap.mc", "array_sum.mc"}) {
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

    // Pointers: address-of, dereference, assignment through a pointer, and
    // null-pointer-constant compatibility are all well-typed.
    {
        const auto diags = analyzeSource(
            "void swap(int *a, int *b) {\n"
            "    int temp = *a;\n"
            "    *a = *b;\n"
            "    *b = temp;\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    int x = 1;\n"
            "    int *p = &x;\n"
            "    int *q = 0;\n"
            "    swap(&x, p);\n"
            "    if (p) {\n"
            "        q = p;\n"
            "    }\n"
            "    return *p;\n"
            "}\n");
        assert(diags.empty());
    }

    // Dereferencing a non-pointer type is an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int x = 5;\n"
            "    return *x;\n"
            "}\n");
        assert(hasError(diags, "cannot dereference non-pointer type 'int'"));
    }

    // Taking the address of a non-lvalue is an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int *p = &5;\n"
            "    return *p;\n"
            "}\n");
        assert(hasError(diags, "cannot take the address of a non-lvalue expression"));
    }

    // Assigning between pointers of different pointee types is an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int x = 1;\n"
            "    float *p = &x;\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "cannot convert 'int*' to 'float*'"));
    }

    // Dereferencing a pointer to an incomplete type (void) is an error.
    {
        const auto diags = analyzeSource(
            "int f(void *p) {\n"
            "    return *p;\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "cannot dereference pointer to incomplete type 'void'"));
    }

    // Arrays: declaration, indexed read/write, address-of an element, and
    // passing an array to a pointer parameter (array-to-pointer decay) are
    // all well-typed.
    {
        const auto diags = analyzeSource(
            "int sum(int *arr, int n) {\n"
            "    int total = 0;\n"
            "    int i = 0;\n"
            "    while (i < n) {\n"
            "        total = total + arr[i];\n"
            "        i = i + 1;\n"
            "    }\n"
            "    return total;\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    int values[5];\n"
            "    values[0] = 1;\n"
            "    int *p = &values[0];\n"
            "    int *q = values;\n"
            "    return sum(values, 5);\n"
            "}\n");
        assert(diags.empty());
    }

    // Arrays are not assignable as a whole.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int a[3];\n"
            "    int b[3];\n"
            "    a = b;\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "array 'a' is not assignable"));
    }

    // Taking the address of an array (no pointer-to-array type) is an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int a[3];\n"
            "    int **p = &a;\n"
            "    return 0;\n"
            "}\n");
        assert(hasError(diags, "cannot take the address of array 'a'"));
    }

    // Indexing a non-array, non-pointer type is an error.
    {
        const auto diags = analyzeSource(
            "int main() {\n"
            "    int x = 5;\n"
            "    return x[0];\n"
            "}\n");
        assert(hasError(diags, "subscripted value is not an array or pointer"));
    }

    // An array declared with a non-positive size is a syntax error, caught
    // at parse time (a Type with arrayLength 0 is indistinguishable from a
    // non-array, so this can't be a sema-level check).
    {
        bool threw = false;
        try {
            minic::Lexer lexer("int main() { int a[0]; return 0; }", "bad.mc");
            minic::Parser parser(lexer.tokenize());
            parser.parseProgram();
        } catch (const std::exception &ex) {
            threw = true;
            assert(std::string(ex.what()).find("array size must be a positive integer") != std::string::npos);
        }
        assert(threw);
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
