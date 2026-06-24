#pragma once

#include "token.h"

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace minic {

enum class TypeKind {
    Int,
    Float,
    Char,
    Void,
    // Pseudo-type assigned to string literals by the semantic analyzer.
    // MiniC has no first-class string type; string literals are only valid
    // as printf arguments.
    String,
};

// A MiniC type: a base kind, a pointer-indirection depth (0 = not a
// pointer, 1 = T*, 2 = T**, ...), and an optional fixed array length (0 =
// not an array). Kept as a small value type rather than a flat enum so
// pointer and array types compose naturally without a separate "pointee
// type" side table.
//
// `arrayLength > 0` means "array of `arrayLength` elements of type {kind,
// pointerDepth}". Only one array dimension is supported (no arrays of
// arrays) and there is no pointer-to-array type — taking the address of an
// array variable is rejected by sema rather than modeled here.
class Type {
public:
    constexpr Type() : kind_(TypeKind::Int), pointerDepth_(0), arrayLength_(0) {}
    constexpr explicit Type(TypeKind kind, int pointerDepth = 0, int arrayLength = 0)
        : kind_(kind), pointerDepth_(pointerDepth), arrayLength_(arrayLength) {}

    constexpr TypeKind kind() const { return kind_; }
    constexpr int pointerDepth() const { return pointerDepth_; }
    constexpr bool isPointer() const { return pointerDepth_ > 0; }
    constexpr Type pointerTo() const { return Type(kind_, pointerDepth_ + 1); }
    // Caller must check isPointer() first.
    constexpr Type pointee() const { return Type(kind_, pointerDepth_ - 1); }

    constexpr bool isArray() const { return arrayLength_ > 0; }
    constexpr int arrayLength() const { return arrayLength_; }
    // This type with the array dimension stripped (same kind/pointerDepth).
    constexpr Type elementType() const { return Type(kind_, pointerDepth_, 0); }
    // The pointer type an array decays to when used as a value.
    constexpr Type decay() const { return Type(kind_, pointerDepth_ + 1, 0); }
    // Builds "array of `length`" from this (non-array) type.
    constexpr Type arrayOf(int length) const { return Type(kind_, pointerDepth_, length); }

    friend constexpr bool operator==(const Type &a, const Type &b) {
        return a.kind_ == b.kind_ && a.pointerDepth_ == b.pointerDepth_ && a.arrayLength_ == b.arrayLength_;
    }
    friend constexpr bool operator!=(const Type &a, const Type &b) { return !(a == b); }

    static const Type Int;
    static const Type Float;
    static const Type Char;
    static const Type Void;
    static const Type String;

private:
    TypeKind kind_;
    int pointerDepth_;
    int arrayLength_;
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
    AddressOf,
    Deref,
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

class IndexExprNode : public ExprNode {
public:
    IndexExprNode(SourceLocation location, ExprPtr base, ExprPtr index);
    void print(std::ostream &out, int indent) const override;

    ExprPtr base;
    ExprPtr index;
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
    AssignStmtNode(SourceLocation location, ExprPtr target, ExprPtr value);
    void print(std::ostream &out, int indent) const override;

    // An lvalue expression: an IdentExprNode or a UnaryOpExprNode{Deref, ...}.
    ExprPtr target;
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
