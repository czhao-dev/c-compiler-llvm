#include "sema.h"

#include <sstream>
#include <utility>

namespace minic {
namespace {

bool isNumericType(Type type) {
    return type == Type::Int || type == Type::Float || type == Type::Char;
}

} // namespace

// ---------------------------------------------------------------------------
// Diagnostic
// ---------------------------------------------------------------------------

std::string Diagnostic::toString() const {
    std::ostringstream out;
    out << location.filename << ':' << location.line << ':' << location.column << ": "
        << (severity == DiagnosticSeverity::Error ? "error" : "warning") << ": " << message;
    return out.str();
}

// ---------------------------------------------------------------------------
// SymbolTable
// ---------------------------------------------------------------------------

void SymbolTable::enterScope() {
    scopes_.emplace_back();
}

void SymbolTable::exitScope() {
    scopes_.pop_back();
}

bool SymbolTable::declare(const std::string &name, Type type) {
    auto &scope = scopes_.back();
    if (scope.count(name) > 0) {
        return false;
    }
    scope.emplace(name, type);
    return true;
}

const Type *SymbolTable::lookup(const std::string &name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// SemanticAnalyzer
// ---------------------------------------------------------------------------

void SemanticAnalyzer::error(SourceLocation location, std::string message) {
    diagnostics_.push_back(Diagnostic{DiagnosticSeverity::Error, std::move(location), std::move(message)});
}

void SemanticAnalyzer::warning(SourceLocation location, std::string message) {
    diagnostics_.push_back(Diagnostic{DiagnosticSeverity::Warning, std::move(location), std::move(message)});
}

std::vector<Diagnostic> SemanticAnalyzer::analyze(const ProgramNode &program) {
    diagnostics_.clear();
    functions_.clear();

    // printf is a built-in: it accepts any argument types and returns int,
    // matching the C standard library signature.
    functions_.emplace("printf", FunctionSignature{Type::Int, {}, /*isVariadic=*/true});

    collectSignatures(program);

    for (const auto &func : program.functions) {
        checkFunction(*func);
    }

    return std::move(diagnostics_);
}

void SemanticAnalyzer::collectSignatures(const ProgramNode &program) {
    for (const auto &func : program.functions) {
        if (func->name == "printf") {
            error(func->location, "cannot redefine built-in function 'printf'");
            continue;
        }
        if (functions_.count(func->name) > 0) {
            error(func->location, "redefinition of function '" + func->name + "'");
            continue;
        }

        FunctionSignature sig;
        sig.returnType = func->returnType;
        for (const auto &param : func->params) {
            sig.paramTypes.push_back(param.type);
        }
        functions_.emplace(func->name, std::move(sig));
    }
}

void SemanticAnalyzer::checkFunction(const FuncDefNode &func) {
    currentFunction_ = &func;
    loopDepth_ = 0;

    symbols_.enterScope();

    for (const auto &param : func.params) {
        if (param.type == Type::Void) {
            error(param.location, "parameter '" + param.name + "' cannot have type 'void'");
            continue;
        }
        if (!symbols_.declare(param.name, param.type)) {
            error(param.location, "redefinition of parameter '" + param.name + "'");
        }
    }

    checkBlock(*func.body);

    symbols_.exitScope();
    currentFunction_ = nullptr;
}

void SemanticAnalyzer::checkBlock(const BlockStmtNode &block) {
    for (const auto &stmt : block.statements) {
        checkStmt(*stmt);
    }
}

void SemanticAnalyzer::checkStmt(const StmtNode &stmt) {
    if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) {
        checkVarDecl(*decl);
    } else if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) {
        checkAssign(*assign);
    } else if (const auto *exprStmt = dynamic_cast<const ExprStmtNode *>(&stmt)) {
        checkExpr(*exprStmt->expr);
    } else if (const auto *ifStmt = dynamic_cast<const IfStmtNode *>(&stmt)) {
        checkIf(*ifStmt);
    } else if (const auto *whileStmt = dynamic_cast<const WhileStmtNode *>(&stmt)) {
        checkWhile(*whileStmt);
    } else if (const auto *forStmt = dynamic_cast<const ForStmtNode *>(&stmt)) {
        checkFor(*forStmt);
    } else if (const auto *ret = dynamic_cast<const ReturnStmtNode *>(&stmt)) {
        checkReturn(*ret);
    } else if (dynamic_cast<const BreakStmtNode *>(&stmt)) {
        if (loopDepth_ == 0) {
            error(stmt.location, "'break' statement not within a loop");
        }
    } else if (dynamic_cast<const ContinueStmtNode *>(&stmt)) {
        if (loopDepth_ == 0) {
            error(stmt.location, "'continue' statement not within a loop");
        }
    } else if (const auto *block = dynamic_cast<const BlockStmtNode *>(&stmt)) {
        symbols_.enterScope();
        checkBlock(*block);
        symbols_.exitScope();
    }
}

void SemanticAnalyzer::checkVarDecl(const VarDeclStmtNode &decl) {
    if (decl.type == Type::Void) {
        error(decl.location, "variable '" + decl.name + "' cannot have type 'void'");
    }

    if (decl.init) {
        const Type valueType = checkExpr(*decl.init);
        checkAssignable(decl.location, decl.type, valueType,
                         "initializer for '" + decl.name + "'");
    }

    if (!symbols_.declare(decl.name, decl.type)) {
        error(decl.location, "redefinition of '" + decl.name + "'");
    }
}

void SemanticAnalyzer::checkAssign(const AssignStmtNode &assign) {
    const Type *targetType = symbols_.lookup(assign.name);
    if (!targetType) {
        error(assign.location, "use of undeclared variable '" + assign.name + "'");
        checkExpr(*assign.value);
        return;
    }

    const Type valueType = checkExpr(*assign.value);
    checkAssignable(assign.location, *targetType, valueType, "assignment to '" + assign.name + "'");
}

void SemanticAnalyzer::checkIf(const IfStmtNode &stmt) {
    checkCondition(stmt.condition->location, checkExpr(*stmt.condition));

    symbols_.enterScope();
    checkBlock(*stmt.thenBlock);
    symbols_.exitScope();

    if (stmt.elseBlock) {
        symbols_.enterScope();
        checkBlock(*stmt.elseBlock);
        symbols_.exitScope();
    }
}

void SemanticAnalyzer::checkWhile(const WhileStmtNode &stmt) {
    checkCondition(stmt.condition->location, checkExpr(*stmt.condition));

    ++loopDepth_;
    symbols_.enterScope();
    checkBlock(*stmt.body);
    symbols_.exitScope();
    --loopDepth_;
}

void SemanticAnalyzer::checkFor(const ForStmtNode &stmt) {
    symbols_.enterScope();

    if (stmt.init) {
        checkStmt(*stmt.init);
    }
    if (stmt.condition) {
        checkCondition(stmt.condition->location, checkExpr(*stmt.condition));
    }
    if (stmt.update) {
        checkStmt(*stmt.update);
    }

    ++loopDepth_;
    symbols_.enterScope();
    checkBlock(*stmt.body);
    symbols_.exitScope();
    --loopDepth_;

    symbols_.exitScope();
}

void SemanticAnalyzer::checkReturn(const ReturnStmtNode &stmt) {
    const Type returnType = currentFunction_->returnType;
    const std::string &funcName = currentFunction_->name;

    if (stmt.value) {
        const Type valueType = checkExpr(*stmt.value);
        if (returnType == Type::Void) {
            error(stmt.location, "void function '" + funcName + "' should not return a value");
        } else {
            checkAssignable(stmt.location, returnType, valueType,
                             "return value of function '" + funcName + "'");
        }
    } else if (returnType != Type::Void) {
        error(stmt.location, "non-void function '" + funcName + "' must return a value");
    }
}

void SemanticAnalyzer::checkCondition(const SourceLocation &location, Type type) {
    if (!isNumericType(type)) {
        error(location, "condition must have a numeric type, got '" + typeName(type) + "'");
    }
}

Type SemanticAnalyzer::checkExpr(const ExprNode &expr) {
    if (dynamic_cast<const IntLitExprNode *>(&expr)) {
        return Type::Int;
    }
    if (dynamic_cast<const FloatLitExprNode *>(&expr)) {
        return Type::Float;
    }
    if (dynamic_cast<const CharLitExprNode *>(&expr)) {
        return Type::Char;
    }
    if (dynamic_cast<const StringLitExprNode *>(&expr)) {
        return Type::String;
    }
    if (const auto *ident = dynamic_cast<const IdentExprNode *>(&expr)) {
        return checkIdent(*ident);
    }
    if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        return checkUnaryOp(*unary);
    }
    if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
        return checkBinOp(*binOp);
    }
    if (const auto *call = dynamic_cast<const CallExprNode *>(&expr)) {
        return checkCall(*call);
    }
    return Type::Int;
}

Type SemanticAnalyzer::checkIdent(const IdentExprNode &expr) {
    const Type *type = symbols_.lookup(expr.name);
    if (!type) {
        error(expr.location, "use of undeclared variable '" + expr.name + "'");
        return Type::Int;
    }
    return *type;
}

Type SemanticAnalyzer::checkUnaryOp(const UnaryOpExprNode &expr) {
    const Type operandType = checkExpr(*expr.operand);
    if (!isNumericType(operandType)) {
        error(expr.location, "invalid operand to unary '" + unaryOpSymbol(expr.op) + "': '" +
                                  typeName(operandType) + "'");
        return Type::Int;
    }

    switch (expr.op) {
    case UnaryOp::Negate: return operandType == Type::Float ? Type::Float : Type::Int;
    case UnaryOp::Not: return Type::Int;
    }
    return Type::Int;
}

Type SemanticAnalyzer::checkBinOp(const BinOpExprNode &expr) {
    const Type lhsType = checkExpr(*expr.lhs);
    const Type rhsType = checkExpr(*expr.rhs);

    if (!isNumericType(lhsType) || !isNumericType(rhsType)) {
        error(expr.location, "invalid operands to binary '" + binaryOpSymbol(expr.op) + "': '" +
                                  typeName(lhsType) + "' and '" + typeName(rhsType) + "'");
        return Type::Int;
    }

    switch (expr.op) {
    case BinaryOp::Add:
    case BinaryOp::Sub:
    case BinaryOp::Mul:
    case BinaryOp::Div:
        return (lhsType == Type::Float || rhsType == Type::Float) ? Type::Float : Type::Int;
    default:
        // Comparisons (==, !=, <, >, <=, >=) and logical operators (&&, ||)
        // all produce an int (0 or 1), matching C.
        return Type::Int;
    }
}

Type SemanticAnalyzer::checkCall(const CallExprNode &expr) {
    auto it = functions_.find(expr.callee);
    if (it == functions_.end()) {
        error(expr.location, "call to undeclared function '" + expr.callee + "'");
        for (const auto &arg : expr.args) {
            checkExpr(*arg);
        }
        return Type::Int;
    }

    const FunctionSignature &sig = it->second;

    if (!sig.isVariadic && expr.args.size() != sig.paramTypes.size()) {
        error(expr.location, "wrong number of arguments to '" + expr.callee + "' — expected " +
                                  std::to_string(sig.paramTypes.size()) + ", got " +
                                  std::to_string(expr.args.size()));
    }

    for (std::size_t i = 0; i < expr.args.size(); ++i) {
        const Type argType = checkExpr(*expr.args[i]);
        if (!sig.isVariadic && i < sig.paramTypes.size()) {
            checkAssignable(expr.args[i]->location, sig.paramTypes[i], argType,
                             "argument " + std::to_string(i + 1) + " to '" + expr.callee + "'");
        }
    }

    return sig.returnType;
}

void SemanticAnalyzer::checkAssignable(const SourceLocation &location, Type target, Type value,
                                        const std::string &context) {
    if (target == value) {
        return;
    }

    if (!isNumericType(target) || !isNumericType(value)) {
        error(location, context + ": cannot convert '" + typeName(value) + "' to '" + typeName(target) + "'");
        return;
    }

    if (value == Type::Float && target != Type::Float) {
        warning(location, context + ": implicit conversion from 'float' to '" + typeName(target) +
                               "' may lose precision");
    }
}

std::string semanticAnalyzerStatus() {
    return "semantic analyzer: scope and type checking implemented";
}

} // namespace minic
