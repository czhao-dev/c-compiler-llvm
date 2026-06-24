#include "lexer.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expectTypes(const std::string &source, const std::vector<minic::TokenType> &expected) {
    minic::Lexer lexer(source);
    const auto tokens = lexer.tokenize();
    assert(tokens.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (tokens[i].type != expected[i]) {
            std::cerr << "token " << i << ": expected "
                      << minic::tokenTypeName(expected[i]) << ", got "
                      << minic::tokenTypeName(tokens[i].type) << '\n';
            std::abort();
        }
    }
}

} // namespace

int main() {
    using minic::TokenType;

    expectTypes(
        "int main() { int x = 5; x = x + 1; return x; }",
        {
            TokenType::Int,
            TokenType::Identifier,
            TokenType::LeftParen,
            TokenType::RightParen,
            TokenType::LeftBrace,
            TokenType::Int,
            TokenType::Identifier,
            TokenType::Assign,
            TokenType::IntLiteral,
            TokenType::Semicolon,
            TokenType::Identifier,
            TokenType::Assign,
            TokenType::Identifier,
            TokenType::Plus,
            TokenType::IntLiteral,
            TokenType::Semicolon,
            TokenType::Return,
            TokenType::Identifier,
            TokenType::Semicolon,
            TokenType::RightBrace,
            TokenType::EndOfFile,
        });

    expectTypes(
        "if (n <= 1 || n != 3 && !done) { printf(\"%d\\n\", n); }",
        {
            TokenType::If,
            TokenType::LeftParen,
            TokenType::Identifier,
            TokenType::LessEqual,
            TokenType::IntLiteral,
            TokenType::Or,
            TokenType::Identifier,
            TokenType::NotEqual,
            TokenType::IntLiteral,
            TokenType::And,
            TokenType::Not,
            TokenType::Identifier,
            TokenType::RightParen,
            TokenType::LeftBrace,
            TokenType::Identifier,
            TokenType::LeftParen,
            TokenType::StringLiteral,
            TokenType::Comma,
            TokenType::Identifier,
            TokenType::RightParen,
            TokenType::Semicolon,
            TokenType::RightBrace,
            TokenType::EndOfFile,
        });

    expectTypes(
        "char c = '\\n'; float f = 1.5; // comment\nfor (;;) { break; continue; }",
        {
            TokenType::Char,
            TokenType::Identifier,
            TokenType::Assign,
            TokenType::CharLiteral,
            TokenType::Semicolon,
            TokenType::Float,
            TokenType::Identifier,
            TokenType::Assign,
            TokenType::FloatLiteral,
            TokenType::Semicolon,
            TokenType::For,
            TokenType::LeftParen,
            TokenType::Semicolon,
            TokenType::Semicolon,
            TokenType::RightParen,
            TokenType::LeftBrace,
            TokenType::Break,
            TokenType::Semicolon,
            TokenType::Continue,
            TokenType::Semicolon,
            TokenType::RightBrace,
            TokenType::EndOfFile,
        });

    expectTypes(
        "int arr[5]; arr[0] = 1;",
        {
            TokenType::Int,
            TokenType::Identifier,
            TokenType::LeftBracket,
            TokenType::IntLiteral,
            TokenType::RightBracket,
            TokenType::Semicolon,
            TokenType::Identifier,
            TokenType::LeftBracket,
            TokenType::IntLiteral,
            TokenType::RightBracket,
            TokenType::Assign,
            TokenType::IntLiteral,
            TokenType::Semicolon,
            TokenType::EndOfFile,
        });

    expectTypes(
        "int *p = &x; int y = *p;",
        {
            TokenType::Int,
            TokenType::Star,
            TokenType::Identifier,
            TokenType::Assign,
            TokenType::Ampersand,
            TokenType::Identifier,
            TokenType::Semicolon,
            TokenType::Int,
            TokenType::Identifier,
            TokenType::Assign,
            TokenType::Star,
            TokenType::Identifier,
            TokenType::Semicolon,
            TokenType::EndOfFile,
        });

    return 0;
}
