#pragma once

#include "token.h"

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace minic {

enum class Type {
    Int,
    Float,
    Char,
    Void,
};

std::string typeName(Type type);

enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Eq,
    Neq,
    Lt,
    Gt,
    Leq,
    Geq,
    And,
    Or,
};

enum class UnaryOp {
    Negate,
    Not,
};

std::string binaryOpSymbol(BinaryOp op);
std::string unaryOpSymbol(UnaryOp op);

// Base class for every expression and statement node. Provides the source
// location used for diagnostics and a virtual print() used by --emit-ast.
class ASTNode {
public:
    explicit ASTNode(SourceLocation location) : location(std::move(location)) {}
    virtual ~ASTNode() = default;

    virtual void print(std::ostream &out, int indent) const = 0;

    SourceLocation location;
};

class ExprNode : public ASTNode {
public:
    using ASTNode::ASTNode;
};

class StmtNode : public ASTNode {
public:
    using ASTNode::ASTNode;
};

using ExprPtr = std::unique_ptr<ExprNode>;
using StmtPtr = std::unique_ptr<StmtNode>;

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

class IntLitExprNode : public ExprNode {
public:
    IntLitExprNode(SourceLocation location, long long value);
    void print(std::ostream &out, int indent) const override;

    long long value;
};

class FloatLitExprNode : public ExprNode {
public:
    FloatLitExprNode(SourceLocation location, double value);
    void print(std::ostream &out, int indent) const override;

    double value;
};

class CharLitExprNode : public ExprNode {
public:
    CharLitExprNode(SourceLocation location, char value);
    void print(std::ostream &out, int indent) const override;

    char value;
};

class StringLitExprNode : public ExprNode {
public:
    StringLitExprNode(SourceLocation location, std::string value);
    void print(std::ostream &out, int indent) const override;

    std::string value;
};

class IdentExprNode : public ExprNode {
public:
    IdentExprNode(SourceLocation location, std::string name);
    void print(std::ostream &out, int indent) const override;

    std::string name;
};

class UnaryOpExprNode : public ExprNode {
public:
    UnaryOpExprNode(SourceLocation location, UnaryOp op, ExprPtr operand);
    void print(std::ostream &out, int indent) const override;

    UnaryOp op;
    ExprPtr operand;
};

class BinOpExprNode : public ExprNode {
public:
    BinOpExprNode(SourceLocation location, BinaryOp op, ExprPtr lhs, ExprPtr rhs);
    void print(std::ostream &out, int indent) const override;

    BinaryOp op;
    ExprPtr lhs;
    ExprPtr rhs;
};

class CallExprNode : public ExprNode {
public:
    CallExprNode(SourceLocation location, std::string callee, std::vector<ExprPtr> args);
    void print(std::ostream &out, int indent) const override;

    std::string callee;
    std::vector<ExprPtr> args;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

class BlockStmtNode : public StmtNode {
public:
    BlockStmtNode(SourceLocation location, std::vector<StmtPtr> statements);
    void print(std::ostream &out, int indent) const override;

    std::vector<StmtPtr> statements;
};

class VarDeclStmtNode : public StmtNode {
public:
    VarDeclStmtNode(SourceLocation location, Type type, std::string name, ExprPtr init);
    void print(std::ostream &out, int indent) const override;

    Type type;
    std::string name;
    ExprPtr init; // may be null
};

class AssignStmtNode : public StmtNode {
public:
    AssignStmtNode(SourceLocation location, std::string name, ExprPtr value);
    void print(std::ostream &out, int indent) const override;

    std::string name;
    ExprPtr value;
};

class ExprStmtNode : public StmtNode {
public:
    ExprStmtNode(SourceLocation location, ExprPtr expr);
    void print(std::ostream &out, int indent) const override;

    ExprPtr expr;
};

class IfStmtNode : public StmtNode {
public:
    IfStmtNode(SourceLocation location, ExprPtr condition,
               std::unique_ptr<BlockStmtNode> thenBlock,
               std::unique_ptr<BlockStmtNode> elseBlock);
    void print(std::ostream &out, int indent) const override;

    ExprPtr condition;
    std::unique_ptr<BlockStmtNode> thenBlock;
    std::unique_ptr<BlockStmtNode> elseBlock; // may be null
};

class WhileStmtNode : public StmtNode {
public:
    WhileStmtNode(SourceLocation location, ExprPtr condition, std::unique_ptr<BlockStmtNode> body);
    void print(std::ostream &out, int indent) const override;

    ExprPtr condition;
    std::unique_ptr<BlockStmtNode> body;
};

class ForStmtNode : public StmtNode {
public:
    ForStmtNode(SourceLocation location, StmtPtr init, ExprPtr condition, StmtPtr update,
                 std::unique_ptr<BlockStmtNode> body);
    void print(std::ostream &out, int indent) const override;

    StmtPtr init;         // VarDeclStmtNode or AssignStmtNode, may be null
    ExprPtr condition;    // may be null
    StmtPtr update;       // AssignStmtNode, may be null
    std::unique_ptr<BlockStmtNode> body;
};

class ReturnStmtNode : public StmtNode {
public:
    ReturnStmtNode(SourceLocation location, ExprPtr value);
    void print(std::ostream &out, int indent) const override;

    ExprPtr value; // may be null
};

class BreakStmtNode : public StmtNode {
public:
    explicit BreakStmtNode(SourceLocation location);
    void print(std::ostream &out, int indent) const override;
};

class ContinueStmtNode : public StmtNode {
public:
    explicit ContinueStmtNode(SourceLocation location);
    void print(std::ostream &out, int indent) const override;
};

// ---------------------------------------------------------------------------
// Top-level
// ---------------------------------------------------------------------------

struct ParamNode {
    Type type;
    std::string name;
    SourceLocation location;

    void print(std::ostream &out, int indent) const;
};

class FuncDefNode {
public:
    FuncDefNode(SourceLocation location, Type returnType, std::string name,
                std::vector<ParamNode> params, std::unique_ptr<BlockStmtNode> body);

    void print(std::ostream &out, int indent) const;

    SourceLocation location;
    Type returnType;
    std::string name;
    std::vector<ParamNode> params;
    std::unique_ptr<BlockStmtNode> body;
};

class ProgramNode {
public:
    void print(std::ostream &out, int indent = 0) const;

    std::vector<std::unique_ptr<FuncDefNode>> functions;
};

} // namespace minic
