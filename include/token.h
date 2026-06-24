#pragma once

#include <ostream>
#include <string>

namespace minic {

enum class TokenType {
    Int,
    Float,
    Char,
    Void,
    If,
    Else,
    While,
    For,
    Return,
    Break,
    Continue,
    Identifier,
    IntLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    Plus,
    Minus,
    Star,
    Slash,
    Equal,
    NotEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    And,
    Or,
    Not,
    Ampersand,
    Assign,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Semicolon,
    Comma,
    EndOfFile,
};

struct SourceLocation {
    std::string filename;
    int line = 1;
    int column = 1;
};

struct Token {
    TokenType type;
    std::string lexeme;
    SourceLocation location;
};

std::string tokenTypeName(TokenType type);
std::ostream &operator<<(std::ostream &out, const Token &token);

} // namespace minic
