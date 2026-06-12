#pragma once

#include "ast.h"
#include "token.h"

#include <string>
#include <vector>

namespace minic {

// Recursive-descent parser. Consumes a flat token stream (as produced by
// Lexer::tokenize()) and builds an AST. Throws std::runtime_error with a
// "file:line:col: error: ..." message on the first syntax error.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    ProgramNode parseProgram();

private:
    // Token stream helpers.
    const Token &peek(int offset = 0) const;
    const Token &advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token &expect(TokenType type, const std::string &message);
    [[noreturn]] void error(const Token &token, const std::string &message) const;

    bool isTypeToken(TokenType type) const;
    Type tokenToType(TokenType type) const;

    // Top-level.
    std::unique_ptr<FuncDefNode> parseFuncDef();
    ParamNode parseParam();

    // Statements.
    std::unique_ptr<BlockStmtNode> parseBlock();
    StmtPtr parseStatement();
    std::unique_ptr<VarDeclStmtNode> parseVarDeclNoSemi();
    StmtPtr parseVarDecl();
    std::unique_ptr<AssignStmtNode> parseAssignNoSemi();
    StmtPtr parseAssignOrExprStmt();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();
    StmtPtr parseForInit();
    StmtPtr parseForUpdate();
    StmtPtr parseReturn();
    StmtPtr parseBreak();
    StmtPtr parseContinue();

    // Expressions, ordered from lowest to highest precedence.
    ExprPtr parseExpression();
    ExprPtr parseLogicalOr();
    ExprPtr parseLogicalAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePrimary();
    ExprPtr parseCallExpr(const Token &calleeTok);

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

} // namespace minic
