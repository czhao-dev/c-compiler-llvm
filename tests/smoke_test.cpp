#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

#include <cassert>
#include <string>

int main() {
    minic::Lexer lexer("int main() { return 0; }");
    const auto tokens = lexer.tokenize();
    assert(!tokens.empty());
    assert(tokens.back().type == minic::TokenType::EndOfFile);

    assert(minic::parserStatus().find("not implemented") != std::string::npos);
    assert(minic::semanticAnalyzerStatus().find("not implemented") != std::string::npos);
    assert(!minic::codegenStatus().empty());
    return 0;
}
