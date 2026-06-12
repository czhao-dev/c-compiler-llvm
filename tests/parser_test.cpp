#include "ast.h"
#include "lexer.h"
#include "parser.h"

#include <cassert>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

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

minic::ProgramNode parseSource(const std::string &source, const std::string &filename = "<input>") {
    minic::Lexer lexer(source, filename);
    minic::Parser parser(lexer.tokenize());
    return parser.parseProgram();
}

minic::ProgramNode parseFile(const std::string &path) {
    return parseSource(readFile(path), path);
}

std::set<std::string> functionNames(const minic::ProgramNode &program) {
    std::set<std::string> names;
    for (const auto &func : program.functions) {
        names.insert(func->name);
    }
    return names;
}

} // namespace

int main() {
    const std::string examplesDir = MINIC_EXAMPLES_DIR;

    // Each example program should parse and print without error.
    {
        auto program = parseFile(examplesDir + "/fibonacci.mc");
        const auto names = functionNames(program);
        assert(names.count("fibonacci") == 1);
        assert(names.count("main") == 1);

        std::ostringstream out;
        program.print(out);
        const std::string ast = out.str();
        assert(ast.find("FuncDef fibonacci -> int") != std::string::npos);
        assert(ast.find("Call fibonacci") != std::string::npos);
        assert(ast.find("BinOp <=") != std::string::npos);
    }

    {
        auto program = parseFile(examplesDir + "/gcd.mc");
        const auto names = functionNames(program);
        assert(names.count("gcd") == 1);
        assert(names.count("main") == 1);
    }

    {
        auto program = parseFile(examplesDir + "/fizzbuzz.mc");
        assert(functionNames(program).count("main") == 1);
    }

    {
        auto program = parseFile(examplesDir + "/sum_of_squares.mc");
        const auto names = functionNames(program);
        assert(names.count("sum_of_squares") == 1);
        assert(names.count("main") == 1);
    }

    // A program that exercises every statement and expression kind.
    {
        const std::string source =
            "int max(int a, int b) {\n"
            "    if (a > b) {\n"
            "        return a;\n"
            "    } else {\n"
            "        return b;\n"
            "    }\n"
            "}\n"
            "\n"
            "int main() {\n"
            "    int total = 0;\n"
            "    for (int i = 0; i < 10; i = i + 1) {\n"
            "        if (i == 5) {\n"
            "            continue;\n"
            "        }\n"
            "        if (i == 8) {\n"
            "            break;\n"
            "        }\n"
            "        total = total + max(i, 1) * 2 - 1;\n"
            "    }\n"
            "    printf(\"%d\\n\", total);\n"
            "    return 0;\n"
            "}\n";

        auto program = parseSource(source);
        assert(program.functions.size() == 2);

        std::ostringstream out;
        program.print(out);
        const std::string ast = out.str();
        assert(ast.find("FuncDef max -> int") != std::string::npos);
        assert(ast.find("Param int a") != std::string::npos);
        assert(ast.find("For") != std::string::npos);
        assert(ast.find("Break") != std::string::npos);
        assert(ast.find("Continue") != std::string::npos);
        assert(ast.find("BinOp ==") != std::string::npos);
        assert(ast.find("BinOp *") != std::string::npos);
        assert(ast.find("Call printf") != std::string::npos);
        assert(ast.find("StringLit") != std::string::npos);
    }

    // Operator precedence: "*" binds tighter than "+", "&&" binds tighter than "||".
    {
        auto program = parseSource(
            "int main() {\n"
            "    int x = 1 + 2 * 3;\n"
            "    int y = 1 || 2 && 3;\n"
            "    return x;\n"
            "}\n");

        const auto &body = program.functions[0]->body->statements;
        const auto *xDecl = dynamic_cast<minic::VarDeclStmtNode *>(body[0].get());
        assert(xDecl != nullptr);
        const auto *xAdd = dynamic_cast<minic::BinOpExprNode *>(xDecl->init.get());
        assert(xAdd != nullptr && xAdd->op == minic::BinaryOp::Add);
        assert(dynamic_cast<minic::BinOpExprNode *>(xAdd->rhs.get())->op == minic::BinaryOp::Mul);

        const auto *yDecl = dynamic_cast<minic::VarDeclStmtNode *>(body[1].get());
        assert(yDecl != nullptr);
        const auto *yOr = dynamic_cast<minic::BinOpExprNode *>(yDecl->init.get());
        assert(yOr != nullptr && yOr->op == minic::BinaryOp::Or);
        assert(dynamic_cast<minic::BinOpExprNode *>(yOr->rhs.get())->op == minic::BinaryOp::And);
    }

    // Unary operators and parenthesized expressions.
    {
        auto program = parseSource(
            "int main() {\n"
            "    int x = -(1 + 2);\n"
            "    int y = !x;\n"
            "    return x;\n"
            "}\n");

        const auto &body = program.functions[0]->body->statements;
        const auto *xDecl = dynamic_cast<minic::VarDeclStmtNode *>(body[0].get());
        const auto *neg = dynamic_cast<minic::UnaryOpExprNode *>(xDecl->init.get());
        assert(neg != nullptr && neg->op == minic::UnaryOp::Negate);
        assert(dynamic_cast<minic::BinOpExprNode *>(neg->operand.get()) != nullptr);

        const auto *yDecl = dynamic_cast<minic::VarDeclStmtNode *>(body[1].get());
        const auto *notExpr = dynamic_cast<minic::UnaryOpExprNode *>(yDecl->init.get());
        assert(notExpr != nullptr && notExpr->op == minic::UnaryOp::Not);
    }

    // Syntax errors are reported with file:line:column.
    {
        bool threw = false;
        try {
            parseSource("int main() { return 0 }", "bad.mc");
        } catch (const std::exception &ex) {
            threw = true;
            const std::string message = ex.what();
            assert(message.find("bad.mc:1:") != std::string::npos);
        }
        assert(threw);
    }

    return 0;
}
