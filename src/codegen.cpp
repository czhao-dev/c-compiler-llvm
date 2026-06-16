#include "codegen.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#if defined(MINIC_HAS_LLVM)
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>

#include <llvm/Config/llvm-config.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>
#endif

namespace minic {

std::string codegenStatus() {
#if defined(MINIC_HAS_LLVM)
    return "codegen: LLVM " LLVM_VERSION_STRING " available";
#else
    return "codegen: LLVM not configured";
#endif
}

#if !defined(MINIC_HAS_LLVM)

std::string emitLLVMIR(const ProgramNode &, const std::string &) {
    throw std::runtime_error("LLVM support is not configured; rebuild with LLVM available");
}

void compileToNative(const ProgramNode &, const std::string &, const std::string &) {
    throw std::runtime_error("LLVM support is not configured; rebuild with LLVM available");
}

#else
namespace {

bool isNumericType(Type type) {
    return type == Type::Int || type == Type::Float || type == Type::Char;
}

bool isComparison(BinaryOp op) {
    return op == BinaryOp::Eq || op == BinaryOp::Neq || op == BinaryOp::Lt ||
           op == BinaryOp::Gt || op == BinaryOp::Leq || op == BinaryOp::Geq;
}

bool isLogical(BinaryOp op) {
    return op == BinaryOp::And || op == BinaryOp::Or;
}

std::string locationString(const SourceLocation &location) {
    std::ostringstream out;
    out << location.filename << ':' << location.line << ':' << location.column;
    return out.str();
}

std::string shellQuote(const std::string &value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

struct FunctionSignature {
    Type returnType = Type::Void;
    std::vector<Type> paramTypes;
    bool isVariadic = false;
};

struct Variable {
    llvm::AllocaInst *alloca = nullptr;
    Type type = Type::Void;
};

struct TypedValue {
    llvm::Value *value = nullptr;
    Type type = Type::Void;
};

class CodeGenerator {
public:
    CodeGenerator(const ProgramNode &program, std::string moduleName, int optLevel)
        : program_(program), context_(), module_(std::make_unique<llvm::Module>(moduleName, context_)),
          builder_(context_), optLevel_(optLevel) {
        module_->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
    }

    std::string generate() {
        collectSignatures();
        declarePrintf();
        declareFunctions();

        for (const auto &func : program_.functions) {
            emitFunction(*func);
        }

        std::string error;
        llvm::raw_string_ostream errorStream(error);
        if (llvm::verifyModule(*module_, &errorStream)) {
            throw std::runtime_error("generated LLVM IR is invalid:\n" + errorStream.str());
        }

        if (optLevel_ > 0) {
            runOptimizationPasses();
        }

        std::string ir;
        llvm::raw_string_ostream out(ir);
        module_->print(out, nullptr);
        return out.str();
    }

private:
    void collectSignatures() {
        functions_.emplace("printf", FunctionSignature{Type::Int, {}, true});
        for (const auto &func : program_.functions) {
            FunctionSignature sig;
            sig.returnType = func->returnType;
            for (const auto &param : func->params) {
                sig.paramTypes.push_back(param.type);
            }
            functions_.emplace(func->name, std::move(sig));
        }
    }

    llvm::Type *toLLVMType(Type type) {
        switch (type) {
        case Type::Int: return llvm::Type::getInt32Ty(context_);
        case Type::Float: return llvm::Type::getFloatTy(context_);
        case Type::Char: return llvm::Type::getInt8Ty(context_);
        case Type::Void: return llvm::Type::getVoidTy(context_);
        case Type::String: return llvm::PointerType::getUnqual(context_);
        }
        return llvm::Type::getVoidTy(context_);
    }

    void declarePrintf() {
        auto *printfType = llvm::FunctionType::get(llvm::Type::getInt32Ty(context_),
                                                   {llvm::PointerType::getUnqual(context_)},
                                                   true);
        llvm::Function::Create(printfType, llvm::Function::ExternalLinkage, "printf", module_.get());
    }

    void declareFunctions() {
        for (const auto &func : program_.functions) {
            std::vector<llvm::Type *> paramTypes;
            for (const auto &param : func->params) {
                paramTypes.push_back(toLLVMType(param.type));
            }

            auto *funcType = llvm::FunctionType::get(toLLVMType(func->returnType), paramTypes, false);
            llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, func->name, module_.get());
        }
    }

    void emitFunction(const FuncDefNode &func) {
        currentFunctionAst_ = &func;
        llvm::Function *llvmFunc = module_->getFunction(func.name);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", llvmFunc);
        builder_.SetInsertPoint(entry);

        scopes_.clear();
        enterScope();

        std::size_t index = 0;
        for (auto &arg : llvmFunc->args()) {
            const ParamNode &param = func.params[index++];
            arg.setName(param.name);

            llvm::AllocaInst *slot = createEntryAlloca(llvmFunc, param.name, param.type);
            builder_.CreateStore(&arg, slot);
            declareVariable(param.name, Variable{slot, param.type});
        }

        emitBlock(*func.body, false);

        if (!currentBlockTerminated()) {
            emitDefaultReturn(func.returnType);
        }

        exitScope();
        currentFunctionAst_ = nullptr;
    }

    void emitDefaultReturn(Type returnType) {
        if (returnType == Type::Void) {
            builder_.CreateRetVoid();
            return;
        }
        builder_.CreateRet(defaultValue(returnType));
    }

    llvm::AllocaInst *createEntryAlloca(llvm::Function *func, const std::string &name, Type type) {
        llvm::IRBuilder<> entryBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
        return entryBuilder.CreateAlloca(toLLVMType(type), nullptr, name);
    }

    llvm::Constant *defaultValue(Type type) {
        switch (type) {
        case Type::Int: return llvm::ConstantInt::get(toLLVMType(type), 0, true);
        case Type::Float: return llvm::ConstantFP::get(toLLVMType(type), 0.0);
        case Type::Char: return llvm::ConstantInt::get(toLLVMType(type), 0, true);
        case Type::Void: return nullptr;
        case Type::String: return llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(context_));
        }
        return nullptr;
    }

    void enterScope() {
        scopes_.emplace_back();
    }

    void exitScope() {
        scopes_.pop_back();
    }

    void declareVariable(const std::string &name, Variable variable) {
        scopes_.back()[name] = variable;
    }

    const Variable *lookupVariable(const std::string &name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    void emitBlock(const BlockStmtNode &block, bool scoped = true) {
        if (scoped) {
            enterScope();
        }

        for (const auto &stmt : block.statements) {
            if (currentBlockTerminated()) {
                break;
            }
            emitStmt(*stmt);
        }

        if (scoped) {
            exitScope();
        }
    }

    void emitStmt(const StmtNode &stmt) {
        if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) {
            emitVarDecl(*decl);
        } else if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) {
            emitAssign(*assign);
        } else if (const auto *exprStmt = dynamic_cast<const ExprStmtNode *>(&stmt)) {
            emitExpr(*exprStmt->expr);
        } else if (const auto *ifStmt = dynamic_cast<const IfStmtNode *>(&stmt)) {
            emitIf(*ifStmt);
        } else if (const auto *whileStmt = dynamic_cast<const WhileStmtNode *>(&stmt)) {
            emitWhile(*whileStmt);
        } else if (const auto *forStmt = dynamic_cast<const ForStmtNode *>(&stmt)) {
            emitFor(*forStmt);
        } else if (const auto *ret = dynamic_cast<const ReturnStmtNode *>(&stmt)) {
            emitReturn(*ret);
        } else if (dynamic_cast<const BreakStmtNode *>(&stmt)) {
            if (breakTargets_.empty()) {
                throw std::runtime_error(locationString(stmt.location) + ": break outside loop reached codegen");
            }
            builder_.CreateBr(breakTargets_.back());
        } else if (dynamic_cast<const ContinueStmtNode *>(&stmt)) {
            if (continueTargets_.empty()) {
                throw std::runtime_error(locationString(stmt.location) + ": continue outside loop reached codegen");
            }
            builder_.CreateBr(continueTargets_.back());
        } else if (const auto *block = dynamic_cast<const BlockStmtNode *>(&stmt)) {
            emitBlock(*block);
        } else {
            throw std::runtime_error(locationString(stmt.location) + ": unsupported statement in codegen");
        }
    }

    void emitVarDecl(const VarDeclStmtNode &decl) {
        llvm::Function *func = builder_.GetInsertBlock()->getParent();
        llvm::AllocaInst *slot = createEntryAlloca(func, decl.name, decl.type);
        declareVariable(decl.name, Variable{slot, decl.type});

        llvm::Value *initial = defaultValue(decl.type);
        if (decl.init) {
            TypedValue emitted = emitExpr(*decl.init);
            initial = castNumeric(emitted.value, emitted.type, decl.type);
        }
        builder_.CreateStore(initial, slot);
    }

    void emitAssign(const AssignStmtNode &assign) {
        const Variable *target = lookupVariable(assign.name);
        if (!target) {
            throw std::runtime_error(locationString(assign.location) +
                                     ": assignment to unknown variable '" + assign.name + "'");
        }

        TypedValue value = emitExpr(*assign.value);
        builder_.CreateStore(castNumeric(value.value, value.type, target->type), target->alloca);
    }

    void emitIf(const IfStmtNode &stmt) {
        llvm::Function *func = builder_.GetInsertBlock()->getParent();
        auto *thenBlock = llvm::BasicBlock::Create(context_, "if.then", func);
        auto *elseBlock = stmt.elseBlock ? llvm::BasicBlock::Create(context_, "if.else", func) : nullptr;
        auto *mergeBlock = llvm::BasicBlock::Create(context_, "if.end", func);

        TypedValue condition = emitExpr(*stmt.condition);
        builder_.CreateCondBr(toBool(condition), thenBlock, elseBlock ? elseBlock : mergeBlock);

        builder_.SetInsertPoint(thenBlock);
        emitBlock(*stmt.thenBlock);
        bool thenTerminated = currentBlockTerminated();
        if (!thenTerminated) {
            builder_.CreateBr(mergeBlock);
        }

        bool elseTerminated = false;
        if (elseBlock) {
            builder_.SetInsertPoint(elseBlock);
            emitBlock(*stmt.elseBlock);
            elseTerminated = currentBlockTerminated();
            if (!elseTerminated) {
                builder_.CreateBr(mergeBlock);
            }
        }

        builder_.SetInsertPoint(mergeBlock);
        if (stmt.elseBlock && thenTerminated && elseTerminated) {
            builder_.CreateUnreachable();
        }
    }

    void emitWhile(const WhileStmtNode &stmt) {
        llvm::Function *func = builder_.GetInsertBlock()->getParent();
        auto *condBlock = llvm::BasicBlock::Create(context_, "while.cond", func);
        auto *bodyBlock = llvm::BasicBlock::Create(context_, "while.body", func);
        auto *afterBlock = llvm::BasicBlock::Create(context_, "while.end", func);

        builder_.CreateBr(condBlock);

        builder_.SetInsertPoint(condBlock);
        TypedValue condition = emitExpr(*stmt.condition);
        builder_.CreateCondBr(toBool(condition), bodyBlock, afterBlock);

        builder_.SetInsertPoint(bodyBlock);
        breakTargets_.push_back(afterBlock);
        continueTargets_.push_back(condBlock);
        emitBlock(*stmt.body);
        continueTargets_.pop_back();
        breakTargets_.pop_back();
        if (!currentBlockTerminated()) {
            builder_.CreateBr(condBlock);
        }

        builder_.SetInsertPoint(afterBlock);
    }

    void emitFor(const ForStmtNode &stmt) {
        enterScope();
        if (stmt.init) {
            emitStmt(*stmt.init);
        }

        llvm::Function *func = builder_.GetInsertBlock()->getParent();
        auto *condBlock = llvm::BasicBlock::Create(context_, "for.cond", func);
        auto *bodyBlock = llvm::BasicBlock::Create(context_, "for.body", func);
        auto *updateBlock = llvm::BasicBlock::Create(context_, "for.update", func);
        auto *afterBlock = llvm::BasicBlock::Create(context_, "for.end", func);

        builder_.CreateBr(condBlock);

        builder_.SetInsertPoint(condBlock);
        if (stmt.condition) {
            TypedValue condition = emitExpr(*stmt.condition);
            builder_.CreateCondBr(toBool(condition), bodyBlock, afterBlock);
        } else {
            builder_.CreateBr(bodyBlock);
        }

        builder_.SetInsertPoint(bodyBlock);
        breakTargets_.push_back(afterBlock);
        continueTargets_.push_back(updateBlock);
        emitBlock(*stmt.body);
        continueTargets_.pop_back();
        breakTargets_.pop_back();
        if (!currentBlockTerminated()) {
            builder_.CreateBr(updateBlock);
        }

        builder_.SetInsertPoint(updateBlock);
        if (stmt.update) {
            emitStmt(*stmt.update);
        }
        if (!currentBlockTerminated()) {
            builder_.CreateBr(condBlock);
        }

        builder_.SetInsertPoint(afterBlock);
        exitScope();
    }

    void emitReturn(const ReturnStmtNode &stmt) {
        if (!currentFunctionAst_) {
            throw std::runtime_error("return outside function reached codegen");
        }

        if (currentFunctionAst_->returnType == Type::Void) {
            builder_.CreateRetVoid();
            return;
        }

        if (!stmt.value) {
            throw std::runtime_error(locationString(stmt.location) + ": missing return value reached codegen");
        }

        TypedValue value = emitExpr(*stmt.value);
        builder_.CreateRet(castNumeric(value.value, value.type, currentFunctionAst_->returnType));
    }

    TypedValue emitExpr(const ExprNode &expr) {
        if (const auto *lit = dynamic_cast<const IntLitExprNode *>(&expr)) {
            return {llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), lit->value, true), Type::Int};
        }
        if (const auto *lit = dynamic_cast<const FloatLitExprNode *>(&expr)) {
            return {llvm::ConstantFP::get(llvm::Type::getFloatTy(context_), lit->value), Type::Float};
        }
        if (const auto *lit = dynamic_cast<const CharLitExprNode *>(&expr)) {
            return {llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_),
                                           static_cast<unsigned char>(lit->value), true),
                    Type::Char};
        }
        if (const auto *lit = dynamic_cast<const StringLitExprNode *>(&expr)) {
            llvm::GlobalVariable *global = builder_.CreateGlobalString(lit->value);
            llvm::Value *zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
            llvm::Value *ptr = builder_.CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, "str");
            return {ptr, Type::String};
        }
        if (const auto *ident = dynamic_cast<const IdentExprNode *>(&expr)) {
            return emitIdent(*ident);
        }
        if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
            return emitUnary(*unary);
        }
        if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
            return emitBinary(*binOp);
        }
        if (const auto *call = dynamic_cast<const CallExprNode *>(&expr)) {
            return emitCall(*call);
        }

        throw std::runtime_error(locationString(expr.location) + ": unsupported expression in codegen");
    }

    TypedValue emitIdent(const IdentExprNode &expr) {
        const Variable *variable = lookupVariable(expr.name);
        if (!variable) {
            throw std::runtime_error(locationString(expr.location) + ": unknown variable '" + expr.name + "'");
        }

        return {builder_.CreateLoad(toLLVMType(variable->type), variable->alloca, expr.name), variable->type};
    }

    TypedValue emitUnary(const UnaryOpExprNode &expr) {
        TypedValue operand = emitExpr(*expr.operand);
        if (!isNumericType(operand.type)) {
            throw std::runtime_error(locationString(expr.location) + ": invalid unary operand in codegen");
        }

        switch (expr.op) {
        case UnaryOp::Negate:
            if (operand.type == Type::Float) {
                return {builder_.CreateFNeg(operand.value, "negtmp"), Type::Float};
            }
            return {builder_.CreateNeg(castNumeric(operand.value, operand.type, Type::Int), "negtmp"), Type::Int};
        case UnaryOp::Not: {
            llvm::Value *notValue = builder_.CreateNot(toBool(operand), "nottmp");
            return {builder_.CreateZExt(notValue, llvm::Type::getInt32Ty(context_), "booltoint"), Type::Int};
        }
        }
        throw std::runtime_error(locationString(expr.location) + ": unknown unary operator in codegen");
    }

    TypedValue emitBinary(const BinOpExprNode &expr) {
        if (isLogical(expr.op)) {
            llvm::Function *func = builder_.GetInsertBlock()->getParent();

            if (expr.op == BinaryOp::And) {
                auto *rhsBlock = llvm::BasicBlock::Create(context_, "and.rhs", func);
                auto *endBlock = llvm::BasicBlock::Create(context_, "and.end", func);

                TypedValue lhs = emitExpr(*expr.lhs);
                llvm::BasicBlock *lhsExit = builder_.GetInsertBlock();
                builder_.CreateCondBr(toBool(lhs), rhsBlock, endBlock);

                builder_.SetInsertPoint(rhsBlock);
                TypedValue rhs = emitExpr(*expr.rhs);
                llvm::BasicBlock *rhsExit = builder_.GetInsertBlock();
                llvm::Value *rhsBool = toBool(rhs);
                builder_.CreateBr(endBlock);

                builder_.SetInsertPoint(endBlock);
                llvm::PHINode *phi = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2, "andtmp");
                phi->addIncoming(llvm::ConstantInt::getFalse(context_), lhsExit);
                phi->addIncoming(rhsBool, rhsExit);
                return {builder_.CreateZExt(phi, llvm::Type::getInt32Ty(context_), "booltoint"), Type::Int};
            } else {
                auto *rhsBlock = llvm::BasicBlock::Create(context_, "or.rhs", func);
                auto *endBlock = llvm::BasicBlock::Create(context_, "or.end", func);

                TypedValue lhs = emitExpr(*expr.lhs);
                llvm::BasicBlock *lhsExit = builder_.GetInsertBlock();
                builder_.CreateCondBr(toBool(lhs), endBlock, rhsBlock);

                builder_.SetInsertPoint(rhsBlock);
                TypedValue rhs = emitExpr(*expr.rhs);
                llvm::BasicBlock *rhsExit = builder_.GetInsertBlock();
                llvm::Value *rhsBool = toBool(rhs);
                builder_.CreateBr(endBlock);

                builder_.SetInsertPoint(endBlock);
                llvm::PHINode *phi = builder_.CreatePHI(llvm::Type::getInt1Ty(context_), 2, "ortmp");
                phi->addIncoming(llvm::ConstantInt::getTrue(context_), lhsExit);
                phi->addIncoming(rhsBool, rhsExit);
                return {builder_.CreateZExt(phi, llvm::Type::getInt32Ty(context_), "booltoint"), Type::Int};
            }
        }

        TypedValue lhs = emitExpr(*expr.lhs);
        TypedValue rhs = emitExpr(*expr.rhs);
        const Type common = commonNumericType(lhs.type, rhs.type);
        llvm::Value *left = castNumeric(lhs.value, lhs.type, common);
        llvm::Value *right = castNumeric(rhs.value, rhs.type, common);

        if (isComparison(expr.op)) {
            llvm::Value *cmp = nullptr;
            if (common == Type::Float) {
                cmp = emitFloatCompare(expr.op, left, right);
            } else {
                cmp = emitIntCompare(expr.op, left, right);
            }
            return {builder_.CreateZExt(cmp, llvm::Type::getInt32Ty(context_), "cmptoint"), Type::Int};
        }

        if (common == Type::Float) {
            switch (expr.op) {
            case BinaryOp::Add: return {builder_.CreateFAdd(left, right, "addtmp"), Type::Float};
            case BinaryOp::Sub: return {builder_.CreateFSub(left, right, "subtmp"), Type::Float};
            case BinaryOp::Mul: return {builder_.CreateFMul(left, right, "multmp"), Type::Float};
            case BinaryOp::Div: return {builder_.CreateFDiv(left, right, "divtmp"), Type::Float};
            default: break;
            }
        } else {
            switch (expr.op) {
            case BinaryOp::Add: return {builder_.CreateAdd(left, right, "addtmp"), Type::Int};
            case BinaryOp::Sub: return {builder_.CreateSub(left, right, "subtmp"), Type::Int};
            case BinaryOp::Mul: return {builder_.CreateMul(left, right, "multmp"), Type::Int};
            case BinaryOp::Div: return {builder_.CreateSDiv(left, right, "divtmp"), Type::Int};
            default: break;
            }
        }

        throw std::runtime_error(locationString(expr.location) + ": unknown binary operator in codegen");
    }

    TypedValue emitCall(const CallExprNode &expr) {
        llvm::Function *callee = module_->getFunction(expr.callee);
        if (!callee) {
            throw std::runtime_error(locationString(expr.location) + ": unknown function '" + expr.callee + "'");
        }

        auto sigIt = functions_.find(expr.callee);
        if (sigIt == functions_.end()) {
            throw std::runtime_error(locationString(expr.location) + ": missing signature for '" + expr.callee + "'");
        }
        const FunctionSignature &sig = sigIt->second;

        std::vector<llvm::Value *> args;
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
            TypedValue arg = emitExpr(*expr.args[i]);
            if (sig.isVariadic) {
                args.push_back(castForPrintf(arg.value, arg.type));
            } else {
                args.push_back(castNumeric(arg.value, arg.type, sig.paramTypes[i]));
            }
        }

        llvm::Value *call = builder_.CreateCall(callee, args,
                                                sig.returnType == Type::Void ? "" : "calltmp");
        return {call, sig.returnType};
    }

    Type commonNumericType(Type lhs, Type rhs) const {
        if (lhs == Type::Float || rhs == Type::Float) {
            return Type::Float;
        }
        return Type::Int;
    }

    llvm::Value *castNumeric(llvm::Value *value, Type from, Type to) {
        if (from == to) {
            return value;
        }
        if (!isNumericType(from) || !isNumericType(to)) {
            throw std::runtime_error("invalid non-numeric conversion from '" + typeName(from) +
                                     "' to '" + typeName(to) + "'");
        }
        if (from == Type::Char && to == Type::Int) {
            return builder_.CreateSExt(value, llvm::Type::getInt32Ty(context_), "sexttmp");
        }
        if (from == Type::Int && to == Type::Char) {
            return builder_.CreateTrunc(value, llvm::Type::getInt8Ty(context_), "trunctmp");
        }
        if (from == Type::Float && to == Type::Int) {
            return builder_.CreateFPToSI(value, llvm::Type::getInt32Ty(context_), "fptositmp");
        }
        if (from == Type::Float && to == Type::Char) {
            return builder_.CreateFPToSI(value, llvm::Type::getInt8Ty(context_), "fptositmp");
        }
        if (from == Type::Int && to == Type::Float) {
            return builder_.CreateSIToFP(value, llvm::Type::getFloatTy(context_), "sitofptmp");
        }
        if (from == Type::Char && to == Type::Float) {
            return builder_.CreateSIToFP(value, llvm::Type::getFloatTy(context_), "sitofptmp");
        }
        throw std::runtime_error("unsupported numeric conversion from '" + typeName(from) +
                                 "' to '" + typeName(to) + "'");
    }

    llvm::Value *castForPrintf(llvm::Value *value, Type type) {
        if (type == Type::Float) {
            return builder_.CreateFPExt(value, llvm::Type::getDoubleTy(context_), "printfdouble");
        }
        if (type == Type::Char) {
            return builder_.CreateSExt(value, llvm::Type::getInt32Ty(context_), "printfchar");
        }
        return value;
    }

    llvm::Value *toBool(TypedValue value) {
        if (value.type == Type::Float) {
            return builder_.CreateFCmpONE(value.value,
                                          llvm::ConstantFP::get(llvm::Type::getFloatTy(context_), 0.0),
                                          "booltmp");
        }
        if (value.type == Type::Char) {
            return builder_.CreateICmpNE(value.value,
                                         llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), 0, true),
                                         "booltmp");
        }
        if (value.type == Type::Int) {
            return builder_.CreateICmpNE(value.value,
                                         llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, true),
                                         "booltmp");
        }
        throw std::runtime_error("cannot convert '" + typeName(value.type) + "' to bool");
    }

    llvm::Value *emitIntCompare(BinaryOp op, llvm::Value *left, llvm::Value *right) {
        switch (op) {
        case BinaryOp::Eq: return builder_.CreateICmpEQ(left, right, "eqtmp");
        case BinaryOp::Neq: return builder_.CreateICmpNE(left, right, "neqtmp");
        case BinaryOp::Lt: return builder_.CreateICmpSLT(left, right, "lttmp");
        case BinaryOp::Gt: return builder_.CreateICmpSGT(left, right, "gttmp");
        case BinaryOp::Leq: return builder_.CreateICmpSLE(left, right, "letmp");
        case BinaryOp::Geq: return builder_.CreateICmpSGE(left, right, "getmp");
        default: break;
        }
        throw std::runtime_error("invalid integer comparison");
    }

    llvm::Value *emitFloatCompare(BinaryOp op, llvm::Value *left, llvm::Value *right) {
        switch (op) {
        case BinaryOp::Eq: return builder_.CreateFCmpOEQ(left, right, "eqtmp");
        case BinaryOp::Neq: return builder_.CreateFCmpONE(left, right, "neqtmp");
        case BinaryOp::Lt: return builder_.CreateFCmpOLT(left, right, "lttmp");
        case BinaryOp::Gt: return builder_.CreateFCmpOGT(left, right, "gttmp");
        case BinaryOp::Leq: return builder_.CreateFCmpOLE(left, right, "letmp");
        case BinaryOp::Geq: return builder_.CreateFCmpOGE(left, right, "getmp");
        default: break;
        }
        throw std::runtime_error("invalid floating-point comparison");
    }

    bool currentBlockTerminated() const {
        llvm::BasicBlock *block = builder_.GetInsertBlock();
        return !block || block->getTerminator() != nullptr;
    }

    void runOptimizationPasses() {
        llvm::PassBuilder pb;
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;

        pb.registerModuleAnalyses(mam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerFunctionAnalyses(fam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);

        llvm::OptimizationLevel level;
        switch (optLevel_) {
        case 1: level = llvm::OptimizationLevel::O1; break;
        case 2: level = llvm::OptimizationLevel::O2; break;
        default: level = llvm::OptimizationLevel::O3; break;
        }

        llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(level);
        mpm.run(*module_, mam);
    }

    const ProgramNode &program_;
    llvm::LLVMContext context_;
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;
    std::unordered_map<std::string, FunctionSignature> functions_;
    std::vector<std::unordered_map<std::string, Variable>> scopes_;
    std::vector<llvm::BasicBlock *> breakTargets_;
    std::vector<llvm::BasicBlock *> continueTargets_;
    const FuncDefNode *currentFunctionAst_ = nullptr;
    int optLevel_ = 0;
};

} // namespace

std::string emitLLVMIR(const ProgramNode &program, const std::string &moduleName, int optLevel) {
    CodeGenerator generator(program, moduleName, optLevel);
    return generator.generate();
}

void compileToNative(const ProgramNode &program, const std::string &outputPath,
                     const std::string &moduleName, int optLevel) {
    const std::string ir = emitLLVMIR(program, moduleName, optLevel);
    const std::string tempTemplate =
        (std::filesystem::temp_directory_path() / "minic_codegen_XXXXXX.ll").string();
    std::vector<char> tempBuffer(tempTemplate.begin(), tempTemplate.end());
    tempBuffer.push_back('\0');

    const int fd = mkstemps(tempBuffer.data(), 3);
    if (fd == -1) {
        throw std::runtime_error("could not create temporary IR file");
    }
    close(fd);
    const std::filesystem::path tempPath(tempBuffer.data());
    {
        std::ofstream out(tempPath);
        if (!out) {
            std::filesystem::remove(tempPath);
            throw std::runtime_error("could not write temporary IR file: " + tempPath.string());
        }
        out << ir;
        if (!out) {
            std::filesystem::remove(tempPath);
            throw std::runtime_error("could not write temporary IR file: " + tempPath.string());
        }
    }

    const std::string command = "clang -Wno-override-module -O" + std::to_string(optLevel) +
                                " " + shellQuote(tempPath.string()) +
                                " -o " + shellQuote(outputPath);
    const int status = std::system(command.c_str());
    std::filesystem::remove(tempPath);
    if (status != 0) {
        throw std::runtime_error("clang failed while compiling generated LLVM IR");
    }
}

#endif

} // namespace minic
