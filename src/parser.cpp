#include "parser.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace minic {
namespace {

char decodeCharLiteral(const std::string &lexeme) {
    if (lexeme.size() == 1) {
        return lexeme[0];
    }
    // Two-character escape sequence: lexeme[0] == '\\'.
    switch (lexeme[1]) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '0': return '\0';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    default: return lexeme[1];
    }
}

std::string decodeStringLiteral(const std::string &lexeme) {
    std::string decoded;
    for (std::size_t i = 0; i < lexeme.size(); ++i) {
        if (lexeme[i] != '\\' || i + 1 >= lexeme.size()) {
            decoded.push_back(lexeme[i]);
            continue;
        }

        const char escape = lexeme[++i];
        switch (escape) {
        case 'n': decoded.push_back('\n'); break;
        case 't': decoded.push_back('\t'); break;
        case 'r': decoded.push_back('\r'); break;
        case '0': decoded.push_back('\0'); break;
        case '\\': decoded.push_back('\\'); break;
        case '\'': decoded.push_back('\''); break;
        case '"': decoded.push_back('"'); break;
        default: decoded.push_back(escape); break;
        }
    }
    return decoded;
}

} // namespace

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
    if (tokens_.empty()) {
        tokens_.push_back(Token{TokenType::EndOfFile, "", SourceLocation{}});
    }
}

// ---------------------------------------------------------------------------
// Token stream helpers
// ---------------------------------------------------------------------------

const Token &Parser::peek(int offset) const {
    std::size_t index = pos_ + static_cast<std::size_t>(offset);
    if (index >= tokens_.size()) {
        index = tokens_.size() - 1;
    }
    return tokens_[index];
}

const Token &Parser::advance() {
    const Token &tok = tokens_[pos_];
    if (pos_ + 1 < tokens_.size()) {
        ++pos_;
    }
    return tok;
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

const Token &Parser::expect(TokenType type, const std::string &message) {
    if (!check(type)) {
        error(peek(), message);
    }
    return advance();
}

void Parser::error(const Token &token, const std::string &message) const {
    std::ostringstream out;
    out << token.location.filename << ':' << token.location.line << ':' << token.location.column
        << ": error: " << message << " (got " << tokenTypeName(token.type);
    if (!token.lexeme.empty()) {
        out << " '" << token.lexeme << "'";
    }
    out << ")";
    throw std::runtime_error(out.str());
}

bool Parser::isTypeToken(TokenType type) const {
    return type == TokenType::Int || type == TokenType::Float || type == TokenType::Char ||
           type == TokenType::Void;
}

Type Parser::tokenToType(TokenType type) const {
    switch (type) {
    case TokenType::Int: return Type::Int;
    case TokenType::Float: return Type::Float;
    case TokenType::Char: return Type::Char;
    case TokenType::Void: return Type::Void;
    default:
        throw std::logic_error("tokenToType: token is not a type keyword");
    }
}

// ---------------------------------------------------------------------------
// Top-level
// ---------------------------------------------------------------------------

ProgramNode Parser::parseProgram() {
    ProgramNode program;
    while (!check(TokenType::EndOfFile)) {
        program.functions.push_back(parseFuncDef());
    }
    return program;
}

std::unique_ptr<FuncDefNode> Parser::parseFuncDef() {
    const Token &typeTok = peek();
    if (!isTypeToken(typeTok.type)) {
        error(typeTok, "expected a return type");
    }
    advance();
    Type returnType = tokenToType(typeTok.type);
    while (match(TokenType::Star)) {
        returnType = returnType.pointerTo();
    }

    const Token &nameTok = expect(TokenType::Identifier, "expected function name");
    expect(TokenType::LeftParen, "expected '(' after function name");

    std::vector<ParamNode> params;
    if (!check(TokenType::RightParen)) {
        params.push_back(parseParam());
        while (match(TokenType::Comma)) {
            params.push_back(parseParam());
        }
    }
    expect(TokenType::RightParen, "expected ')' after parameter list");

    auto body = parseBlock();
    return std::make_unique<FuncDefNode>(typeTok.location, returnType, nameTok.lexeme,
                                          std::move(params), std::move(body));
}

ParamNode Parser::parseParam() {
    const Token &typeTok = peek();
    if (!isTypeToken(typeTok.type)) {
        error(typeTok, "expected parameter type");
    }
    advance();
    Type type = tokenToType(typeTok.type);
    while (match(TokenType::Star)) {
        type = type.pointerTo();
    }
    const Token &nameTok = expect(TokenType::Identifier, "expected parameter name");
    return ParamNode{type, nameTok.lexeme, typeTok.location};
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

std::unique_ptr<BlockStmtNode> Parser::parseBlock() {
    const Token &braceTok = expect(TokenType::LeftBrace, "expected '{'");
    std::vector<StmtPtr> statements;
    while (!check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
        statements.push_back(parseStatement());
    }
    expect(TokenType::RightBrace, "expected '}'");
    return std::make_unique<BlockStmtNode>(braceTok.location, std::move(statements));
}

StmtPtr Parser::parseStatement() {
    switch (peek().type) {
    case TokenType::Int:
    case TokenType::Float:
    case TokenType::Char:
    case TokenType::Void:
        return parseVarDecl();
    case TokenType::If:
        return parseIf();
    case TokenType::While:
        return parseWhile();
    case TokenType::For:
        return parseFor();
    case TokenType::Return:
        return parseReturn();
    case TokenType::Break:
        return parseBreak();
    case TokenType::Continue:
        return parseContinue();
    case TokenType::Identifier:
    case TokenType::Star:
        return parseAssignOrExprStmt();
    default:
        error(peek(), "expected a statement");
    }
}

std::unique_ptr<VarDeclStmtNode> Parser::parseVarDeclNoSemi() {
    const Token &typeTok = advance();
    Type type = tokenToType(typeTok.type);
    while (match(TokenType::Star)) {
        type = type.pointerTo();
    }
    const Token &nameTok = expect(TokenType::Identifier, "expected variable name");

    if (match(TokenType::LeftBracket)) {
        const Token &sizeTok = expect(TokenType::IntLiteral, "expected array size");
        expect(TokenType::RightBracket, "expected ']' after array size");
        const long long size = std::stoll(sizeTok.lexeme);
        // A Type's arrayLength of 0 means "not an array" (see Type::isArray
        // in ast.h), so a literal size of 0 can't be represented as an
        // array type at all — reject it here rather than silently parsing
        // `int a[0];` as the scalar declaration `int a;`.
        if (size <= 0) {
            error(sizeTok, "array size must be a positive integer");
        }
        type = type.arrayOf(static_cast<int>(size));
    }

    ExprPtr init;
    if (match(TokenType::Assign)) {
        init = parseExpression();
    }
    return std::make_unique<VarDeclStmtNode>(typeTok.location, type, nameTok.lexeme, std::move(init));
}

StmtPtr Parser::parseVarDecl() {
    auto decl = parseVarDeclNoSemi();
    expect(TokenType::Semicolon, "expected ';' after variable declaration");
    return decl;
}

std::unique_ptr<AssignStmtNode> Parser::parseAssignNoSemi() {
    const Token &startTok = peek();
    ExprPtr target = parseUnary();
    expect(TokenType::Assign, "expected '=' in assignment");
    ExprPtr value = parseExpression();
    return std::make_unique<AssignStmtNode>(startTok.location, std::move(target), std::move(value));
}

StmtPtr Parser::parseAssignOrExprStmt() {
    const Token &startTok = peek();
    ExprPtr expr = parseUnary();

    if (check(TokenType::Assign)) {
        advance();
        ExprPtr value = parseExpression();
        expect(TokenType::Semicolon, "expected ';' after assignment");
        return std::make_unique<AssignStmtNode>(startTok.location, std::move(expr), std::move(value));
    }

    if (dynamic_cast<const CallExprNode *>(expr.get())) {
        expect(TokenType::Semicolon, "expected ';' after expression statement");
        return std::make_unique<ExprStmtNode>(startTok.location, std::move(expr));
    }

    error(peek(), "expected '=' or '(' after expression");
}

StmtPtr Parser::parseIf() {
    const Token &ifTok = expect(TokenType::If, "expected 'if'");
    expect(TokenType::LeftParen, "expected '(' after 'if'");
    ExprPtr condition = parseExpression();
    expect(TokenType::RightParen, "expected ')' after condition");

    auto thenBlock = parseBlock();

    std::unique_ptr<BlockStmtNode> elseBlock;
    if (match(TokenType::Else)) {
        if (check(TokenType::If)) {
            // Treat "else if" as an else-block containing a single nested if.
            const SourceLocation loc = peek().location;
            std::vector<StmtPtr> stmts;
            stmts.push_back(parseIf());
            elseBlock = std::make_unique<BlockStmtNode>(loc, std::move(stmts));
        } else {
            elseBlock = parseBlock();
        }
    }

    return std::make_unique<IfStmtNode>(ifTok.location, std::move(condition), std::move(thenBlock),
                                         std::move(elseBlock));
}

StmtPtr Parser::parseWhile() {
    const Token &whileTok = expect(TokenType::While, "expected 'while'");
    expect(TokenType::LeftParen, "expected '(' after 'while'");
    ExprPtr condition = parseExpression();
    expect(TokenType::RightParen, "expected ')' after condition");
    auto body = parseBlock();
    return std::make_unique<WhileStmtNode>(whileTok.location, std::move(condition), std::move(body));
}

StmtPtr Parser::parseFor() {
    const Token &forTok = expect(TokenType::For, "expected 'for'");
    expect(TokenType::LeftParen, "expected '(' after 'for'");

    StmtPtr init;
    if (!check(TokenType::Semicolon)) {
        init = parseForInit();
    }
    expect(TokenType::Semicolon, "expected ';' after for-loop initializer");

    ExprPtr condition;
    if (!check(TokenType::Semicolon)) {
        condition = parseExpression();
    }
    expect(TokenType::Semicolon, "expected ';' after for-loop condition");

    StmtPtr update;
    if (!check(TokenType::RightParen)) {
        update = parseForUpdate();
    }
    expect(TokenType::RightParen, "expected ')' after for-loop update");

    auto body = parseBlock();
    return std::make_unique<ForStmtNode>(forTok.location, std::move(init), std::move(condition),
                                          std::move(update), std::move(body));
}

StmtPtr Parser::parseForInit() {
    if (isTypeToken(peek().type)) {
        return parseVarDeclNoSemi();
    }
    return parseAssignNoSemi();
}

StmtPtr Parser::parseForUpdate() {
    return parseAssignNoSemi();
}

StmtPtr Parser::parseReturn() {
    const Token &returnTok = expect(TokenType::Return, "expected 'return'");
    ExprPtr value;
    if (!check(TokenType::Semicolon)) {
        value = parseExpression();
    }
    expect(TokenType::Semicolon, "expected ';' after return statement");
    return std::make_unique<ReturnStmtNode>(returnTok.location, std::move(value));
}

StmtPtr Parser::parseBreak() {
    const Token &breakTok = expect(TokenType::Break, "expected 'break'");
    expect(TokenType::Semicolon, "expected ';' after 'break'");
    return std::make_unique<BreakStmtNode>(breakTok.location);
}

StmtPtr Parser::parseContinue() {
    const Token &continueTok = expect(TokenType::Continue, "expected 'continue'");
    expect(TokenType::Semicolon, "expected ';' after 'continue'");
    return std::make_unique<ContinueStmtNode>(continueTok.location);
}

// ---------------------------------------------------------------------------
// Expressions
//
// Precedence, lowest to highest: || , && , == != , < > <= >= , + - , * / ,
// unary ! - & * (address-of / deref), postfix [] (indexing), primary.
// ---------------------------------------------------------------------------

ExprPtr Parser::parseExpression() {
    return parseLogicalOr();
}

ExprPtr Parser::parseLogicalOr() {
    ExprPtr left = parseLogicalAnd();
    while (check(TokenType::Or)) {
        const Token &opTok = advance();
        ExprPtr right = parseLogicalAnd();
        left = std::make_unique<BinOpExprNode>(opTok.location, BinaryOp::Or, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parseLogicalAnd() {
    ExprPtr left = parseEquality();
    while (check(TokenType::And)) {
        const Token &opTok = advance();
        ExprPtr right = parseEquality();
        left = std::make_unique<BinOpExprNode>(opTok.location, BinaryOp::And, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parseEquality() {
    ExprPtr left = parseComparison();
    while (check(TokenType::Equal) || check(TokenType::NotEqual)) {
        const Token &opTok = advance();
        const BinaryOp op = opTok.type == TokenType::Equal ? BinaryOp::Eq : BinaryOp::Neq;
        ExprPtr right = parseComparison();
        left = std::make_unique<BinOpExprNode>(opTok.location, op, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseAdditive();
    while (check(TokenType::Less) || check(TokenType::Greater) || check(TokenType::LessEqual) ||
           check(TokenType::GreaterEqual)) {
        const Token &opTok = advance();
        BinaryOp op;
        switch (opTok.type) {
        case TokenType::Less: op = BinaryOp::Lt; break;
        case TokenType::Greater: op = BinaryOp::Gt; break;
        case TokenType::LessEqual: op = BinaryOp::Leq; break;
        default: op = BinaryOp::Geq; break;
        }
        ExprPtr right = parseAdditive();
        left = std::make_unique<BinOpExprNode>(opTok.location, op, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        const Token &opTok = advance();
        const BinaryOp op = opTok.type == TokenType::Plus ? BinaryOp::Add : BinaryOp::Sub;
        ExprPtr right = parseMultiplicative();
        left = std::make_unique<BinOpExprNode>(opTok.location, op, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parseUnary();
    while (check(TokenType::Star) || check(TokenType::Slash)) {
        const Token &opTok = advance();
        const BinaryOp op = opTok.type == TokenType::Star ? BinaryOp::Mul : BinaryOp::Div;
        ExprPtr right = parseUnary();
        left = std::make_unique<BinOpExprNode>(opTok.location, op, std::move(left), std::move(right));
    }
    return left;
}

ExprPtr Parser::parseUnary() {
    if (check(TokenType::Not) || check(TokenType::Minus)) {
        const Token &opTok = advance();
        const UnaryOp op = opTok.type == TokenType::Not ? UnaryOp::Not : UnaryOp::Negate;
        ExprPtr operand = parseUnary();
        return std::make_unique<UnaryOpExprNode>(opTok.location, op, std::move(operand));
    }
    if (check(TokenType::Ampersand) || check(TokenType::Star)) {
        const Token &opTok = advance();
        const UnaryOp op = opTok.type == TokenType::Ampersand ? UnaryOp::AddressOf : UnaryOp::Deref;
        ExprPtr operand = parseUnary();
        return std::make_unique<UnaryOpExprNode>(opTok.location, op, std::move(operand));
    }
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    while (check(TokenType::LeftBracket)) {
        const Token &bracketTok = advance();
        ExprPtr index = parseExpression();
        expect(TokenType::RightBracket, "expected ']' after array index");
        expr = std::make_unique<IndexExprNode>(bracketTok.location, std::move(expr), std::move(index));
    }
    return expr;
}

ExprPtr Parser::parsePrimary() {
    const Token &tok = peek();
    switch (tok.type) {
    case TokenType::IntLiteral:
        advance();
        return std::make_unique<IntLitExprNode>(tok.location, std::stoll(tok.lexeme));
    case TokenType::FloatLiteral:
        advance();
        return std::make_unique<FloatLitExprNode>(tok.location, std::stod(tok.lexeme));
    case TokenType::CharLiteral:
        advance();
        return std::make_unique<CharLitExprNode>(tok.location, decodeCharLiteral(tok.lexeme));
    case TokenType::StringLiteral:
        advance();
        return std::make_unique<StringLitExprNode>(tok.location, decodeStringLiteral(tok.lexeme));
    case TokenType::Identifier: {
        advance();
        if (check(TokenType::LeftParen)) {
            return parseCallExpr(tok);
        }
        return std::make_unique<IdentExprNode>(tok.location, tok.lexeme);
    }
    case TokenType::LeftParen: {
        advance();
        ExprPtr expr = parseExpression();
        expect(TokenType::RightParen, "expected ')' after expression");
        return expr;
    }
    default:
        error(tok, "expected an expression");
    }
}

ExprPtr Parser::parseCallExpr(const Token &calleeTok) {
    expect(TokenType::LeftParen, "expected '(' in call expression");
    std::vector<ExprPtr> args;
    if (!check(TokenType::RightParen)) {
        args.push_back(parseExpression());
        while (match(TokenType::Comma)) {
            args.push_back(parseExpression());
        }
    }
    expect(TokenType::RightParen, "expected ')' after argument list");
    return std::make_unique<CallExprNode>(calleeTok.location, calleeTok.lexeme, std::move(args));
}

} // namespace minic
