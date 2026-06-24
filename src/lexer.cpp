#include "lexer.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace minic {
namespace {

const std::unordered_map<std::string, TokenType> kKeywords = {
    {"int", TokenType::Int},
    {"float", TokenType::Float},
    {"char", TokenType::Char},
    {"void", TokenType::Void},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"while", TokenType::While},
    {"for", TokenType::For},
    {"return", TokenType::Return},
    {"break", TokenType::Break},
    {"continue", TokenType::Continue},
};

bool isIdentifierStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool isIdentifierBody(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

} // namespace

std::string tokenTypeName(TokenType type) {
    switch (type) {
    case TokenType::Int: return "TOK_INT";
    case TokenType::Float: return "TOK_FLOAT";
    case TokenType::Char: return "TOK_CHAR";
    case TokenType::Void: return "TOK_VOID";
    case TokenType::If: return "TOK_IF";
    case TokenType::Else: return "TOK_ELSE";
    case TokenType::While: return "TOK_WHILE";
    case TokenType::For: return "TOK_FOR";
    case TokenType::Return: return "TOK_RETURN";
    case TokenType::Break: return "TOK_BREAK";
    case TokenType::Continue: return "TOK_CONTINUE";
    case TokenType::Identifier: return "TOK_IDENT";
    case TokenType::IntLiteral: return "TOK_INT_LIT";
    case TokenType::FloatLiteral: return "TOK_FLOAT_LIT";
    case TokenType::CharLiteral: return "TOK_CHAR_LIT";
    case TokenType::StringLiteral: return "TOK_STRING_LIT";
    case TokenType::Plus: return "TOK_PLUS";
    case TokenType::Minus: return "TOK_MINUS";
    case TokenType::Star: return "TOK_STAR";
    case TokenType::Slash: return "TOK_SLASH";
    case TokenType::Equal: return "TOK_EQ";
    case TokenType::NotEqual: return "TOK_NEQ";
    case TokenType::Less: return "TOK_LT";
    case TokenType::Greater: return "TOK_GT";
    case TokenType::LessEqual: return "TOK_LEQ";
    case TokenType::GreaterEqual: return "TOK_GEQ";
    case TokenType::And: return "TOK_AND";
    case TokenType::Or: return "TOK_OR";
    case TokenType::Not: return "TOK_NOT";
    case TokenType::Ampersand: return "TOK_AMP";
    case TokenType::Assign: return "TOK_ASSIGN";
    case TokenType::LeftParen: return "TOK_LPAREN";
    case TokenType::RightParen: return "TOK_RPAREN";
    case TokenType::LeftBrace: return "TOK_LBRACE";
    case TokenType::RightBrace: return "TOK_RBRACE";
    case TokenType::LeftBracket: return "TOK_LBRACKET";
    case TokenType::RightBracket: return "TOK_RBRACKET";
    case TokenType::Semicolon: return "TOK_SEMI";
    case TokenType::Comma: return "TOK_COMMA";
    case TokenType::EndOfFile: return "TOK_EOF";
    }
    return "TOK_UNKNOWN";
}

std::ostream &operator<<(std::ostream &out, const Token &token) {
    out << token.location.line << ':' << token.location.column << ' '
        << tokenTypeName(token.type);
    if (!token.lexeme.empty()) {
        out << " \"" << token.lexeme << '"';
    }
    return out;
}

Lexer::Lexer(std::string source, std::string filename)
    : source_(std::move(source)), filename_(std::move(filename)) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = nextToken();
        tokens.push_back(token);
        if (token.type == TokenType::EndOfFile) {
            break;
        }
    }
    return tokens;
}

Token Lexer::nextToken() {
    skipWhitespaceAndComments();

    const int startLine = line_;
    const int startColumn = column_;

    if (isAtEnd()) {
        return makeToken(TokenType::EndOfFile, "", startLine, startColumn);
    }

    const char ch = advance();
    if (isIdentifierStart(ch)) {
        return lexIdentifierOrKeyword(startLine, startColumn);
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
        return lexNumber(startLine, startColumn);
    }

    switch (ch) {
    case '+': return makeToken(TokenType::Plus, "+", startLine, startColumn);
    case '-': return makeToken(TokenType::Minus, "-", startLine, startColumn);
    case '*': return makeToken(TokenType::Star, "*", startLine, startColumn);
    case '/': return makeToken(TokenType::Slash, "/", startLine, startColumn);
    case '=': {
        const bool isEqual = match('=');
        return makeToken(isEqual ? TokenType::Equal : TokenType::Assign,
                         isEqual ? "==" : "=",
                         startLine, startColumn);
    }
    case '!': {
        const bool hasEqual = match('=');
        return makeToken(hasEqual ? TokenType::NotEqual : TokenType::Not,
                         hasEqual ? "!=" : "!",
                         startLine, startColumn);
    }
    case '<': {
        const bool hasEqual = match('=');
        return makeToken(hasEqual ? TokenType::LessEqual : TokenType::Less,
                         hasEqual ? "<=" : "<",
                         startLine, startColumn);
    }
    case '>': {
        const bool hasEqual = match('=');
        return makeToken(hasEqual ? TokenType::GreaterEqual : TokenType::Greater,
                         hasEqual ? ">=" : ">",
                         startLine, startColumn);
    }
    case '&':
        if (match('&')) {
            return makeToken(TokenType::And, "&&", startLine, startColumn);
        }
        return makeToken(TokenType::Ampersand, "&", startLine, startColumn);
    case '|':
        if (match('|')) {
            return makeToken(TokenType::Or, "||", startLine, startColumn);
        }
        error(startLine, startColumn, "expected '|' after '|'");
    case '(': return makeToken(TokenType::LeftParen, "(", startLine, startColumn);
    case ')': return makeToken(TokenType::RightParen, ")", startLine, startColumn);
    case '{': return makeToken(TokenType::LeftBrace, "{", startLine, startColumn);
    case '}': return makeToken(TokenType::RightBrace, "}", startLine, startColumn);
    case '[': return makeToken(TokenType::LeftBracket, "[", startLine, startColumn);
    case ']': return makeToken(TokenType::RightBracket, "]", startLine, startColumn);
    case ';': return makeToken(TokenType::Semicolon, ";", startLine, startColumn);
    case ',': return makeToken(TokenType::Comma, ",", startLine, startColumn);
    case '\'': return lexCharLiteral(startLine, startColumn);
    case '"': return lexStringLiteral(startLine, startColumn);
    default:
        error(startLine, startColumn, "unexpected character '" + std::string(1, ch) + "'");
    }
}

bool Lexer::isAtEnd() const {
    return pos_ >= source_.size();
}

char Lexer::peek() const {
    return isAtEnd() ? '\0' : source_[pos_];
}

char Lexer::peekNext() const {
    return pos_ + 1 >= source_.size() ? '\0' : source_[pos_ + 1];
}

char Lexer::advance() {
    const char ch = source_[pos_++];
    if (ch == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return ch;
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[pos_] != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skipWhitespaceAndComments() {
    while (!isAtEnd()) {
        const char ch = peek();
        if (ch == ' ' || ch == '\r' || ch == '\t' || ch == '\n') {
            advance();
            continue;
        }
        if (ch == '/' && peekNext() == '/') {
            while (!isAtEnd() && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::makeToken(TokenType type, std::string lexeme, int startLine, int startColumn) const {
    return Token{type, std::move(lexeme), SourceLocation{filename_, startLine, startColumn}};
}

Token Lexer::lexIdentifierOrKeyword(int startLine, int startColumn) {
    const std::size_t start = pos_ - 1;
    while (isIdentifierBody(peek())) {
        advance();
    }
    std::string text = source_.substr(start, pos_ - start);
    const auto keyword = kKeywords.find(text);
    if (keyword != kKeywords.end()) {
        return makeToken(keyword->second, text, startLine, startColumn);
    }
    return makeToken(TokenType::Identifier, text, startLine, startColumn);
}

Token Lexer::lexNumber(int startLine, int startColumn) {
    const std::size_t start = pos_ - 1;
    while (std::isdigit(static_cast<unsigned char>(peek()))) {
        advance();
    }

    bool isFloat = false;
    if (peek() == '.') {
        isFloat = true;
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            error(startLine, startColumn, "expected digits after exponent in float literal");
        }
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    }

    return makeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral,
                     source_.substr(start, pos_ - start),
                     startLine,
                     startColumn);
}

Token Lexer::lexCharLiteral(int startLine, int startColumn) {
    std::string value;
    if (isAtEnd() || peek() == '\n') {
        error(startLine, startColumn, "unterminated character literal");
    }

    value.push_back(advance());
    if (value.front() == '\\') {
        if (isAtEnd() || peek() == '\n') {
            error(startLine, startColumn, "unterminated character escape");
        }
        value.push_back(advance());
    }

    if (!match('\'')) {
        error(startLine, startColumn, "character literal must contain exactly one character");
    }
    return makeToken(TokenType::CharLiteral, value, startLine, startColumn);
}

Token Lexer::lexStringLiteral(int startLine, int startColumn) {
    std::string value;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            error(startLine, startColumn, "unterminated string literal");
        }
        const char ch = advance();
        value.push_back(ch);
        if (ch == '\\' && !isAtEnd()) {
            value.push_back(advance());
        }
    }

    if (!match('"')) {
        error(startLine, startColumn, "unterminated string literal");
    }
    return makeToken(TokenType::StringLiteral, value, startLine, startColumn);
}

void Lexer::error(int line, int column, const std::string &message) const {
    std::ostringstream out;
    out << filename_ << ':' << line << ':' << column << ": error: " << message;
    throw std::runtime_error(out.str());
}

} // namespace minic
