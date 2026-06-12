#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

#include <fstream>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string inputPath;
    std::string outputPath = "a.out";
    bool emitTokens = false;
    bool emitAst = false;
    bool emitIr = false;
    bool showVersion = false;
};

std::string readFile(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open input file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void printUsage(std::ostream &out) {
    out << "usage: minic <source.mc> [--emit-tokens] [--emit-ast] [--emit-ir] [-o output]\n";
}

Options parseArgs(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage(std::cout);
            std::exit(0);
        }
        if (arg == "--version") {
            options.showVersion = true;
            continue;
        }
        if (arg == "--emit-tokens") {
            options.emitTokens = true;
            continue;
        }
        if (arg == "--emit-ast") {
            options.emitAst = true;
            continue;
        }
        if (arg == "--emit-ir") {
            options.emitIr = true;
            continue;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) {
                throw std::runtime_error("-o requires an output path");
            }
            options.outputPath = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            throw std::runtime_error("unknown option: " + arg);
        }
        if (!options.inputPath.empty()) {
            throw std::runtime_error("multiple input files were provided");
        }
        options.inputPath = arg;
    }
    return options;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parseArgs(argc, argv);
        if (options.showVersion) {
            std::cout << "MiniC compiler starter\n";
            std::cout << minic::codegenStatus() << '\n';
            return 0;
        }

        if (options.inputPath.empty()) {
            printUsage(std::cerr);
            return 2;
        }

        const std::string source = readFile(options.inputPath);

        if (options.emitTokens) {
            minic::Lexer lexer(source, options.inputPath);
            for (const auto &token : lexer.tokenize()) {
                std::cout << token << '\n';
            }
            return 0;
        }

        if (options.emitAst) {
            minic::Lexer lexer(source, options.inputPath);
            minic::Parser parser(lexer.tokenize());
            const minic::ProgramNode program = parser.parseProgram();
            program.print(std::cout);
            return 0;
        }

        if (options.emitIr) {
            std::cerr << minic::codegenStatus() << '\n';
            std::cerr << "IR emission will be added after the semantic analysis phase.\n";
            return 1;
        }

        std::cerr << "no compilation stage selected yet; try --emit-tokens\n";
        return 1;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
