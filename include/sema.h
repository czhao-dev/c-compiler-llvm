#pragma once

#include "ast.h"
#include "token.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace minic {

enum class DiagnosticSeverity {
    Warning,
    Error,
};

// A single semantic error or warning, with the source location it applies to.
struct Diagnostic {
    DiagnosticSeverity severity;
    SourceLocation location;
    std::string message;

    // Formats as "file:line:col: error: message" (or "warning:").
    std::string toString() const;
};

// Scoped symbol table mapping variable names to their declared types.
// Scopes are pushed/popped as the analyzer enters and leaves blocks; lookup
// searches from the innermost scope outward.
class SymbolTable {
public:
    void enterScope();
    void exitScope();

    // Declares `name` in the innermost scope. Returns false (and does not
    // declare) if `name` is already declared in that scope.
    bool declare(const std::string &name, Type type);

    // Returns the declared type of `name`, searching from the innermost
    // scope outward, or nullptr if `name` is not in scope.
    const Type *lookup(const std::string &name) const;

private:
    std::vector<std::unordered_map<std::string, Type>> scopes_;
};

// Walks a parsed program and checks scoping and type rules. Diagnostics are
// collected rather than thrown so a single run can report every problem.
class SemanticAnalyzer {
public:
    std::vector<Diagnostic> analyze(const ProgramNode &program);

private:
    struct FunctionSignature {
        Type returnType;
        std::vector<Type> paramTypes;
        bool isVariadic = false;
    };

    void collectSignatures(const ProgramNode &program);
    void checkFunction(const FuncDefNode &func);

    void checkBlock(const BlockStmtNode &block);
    void checkStmt(const StmtNode &stmt);
    void checkVarDecl(const VarDeclStmtNode &decl);
    void checkAssign(const AssignStmtNode &assign);
    void checkIf(const IfStmtNode &stmt);
    void checkWhile(const WhileStmtNode &stmt);
    void checkFor(const ForStmtNode &stmt);
    void checkReturn(const ReturnStmtNode &stmt);
    void checkCondition(const SourceLocation &location, Type type);

    Type checkExpr(const ExprNode &expr);
    Type checkIdent(const IdentExprNode &expr);
    Type checkUnaryOp(const UnaryOpExprNode &expr);
    Type checkBinOp(const BinOpExprNode &expr);
    Type checkCall(const CallExprNode &expr);
    Type checkIndex(const IndexExprNode &expr);

    // Checks that a value of type `value` may be stored into (or returned
    // as, or passed as an argument of) type `target`. Reports an error for
    // incompatible types, or a warning for a narrowing float -> int/char
    // conversion. `context` is prepended to any diagnostic message.
    // `valueExpr`, if given, lets a literal `0` be accepted as a null
    // pointer constant when `target` is a pointer type.
    void checkAssignable(const SourceLocation &location, Type target, Type value,
                          const std::string &context, const ExprNode *valueExpr = nullptr);

    void error(SourceLocation location, std::string message);
    void warning(SourceLocation location, std::string message);

    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<std::string, FunctionSignature> functions_;
    SymbolTable symbols_;
    const FuncDefNode *currentFunction_ = nullptr;
    int loopDepth_ = 0;
};

std::string semanticAnalyzerStatus();

} // namespace minic
