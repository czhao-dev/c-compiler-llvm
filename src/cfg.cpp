#include "cfg.h"

#include <sstream>

namespace minic {
namespace {

std::string escapeChar(char c) {
    switch (c) {
    case '\n': return "\\n";
    case '\t': return "\\t";
    case '\\': return "\\\\";
    case '\'': return "\\'";
    default: return std::string(1, c);
    }
}

std::string escapeString(const std::string &value) {
    std::string out;
    for (char c : value) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        default: out += c; break;
        }
    }
    return out;
}

// Wraps `expr`'s rendering in parentheses if it's a binary or unary
// expression, so nested operators in a label are unambiguous.
std::string exprToStringParen(const ExprNode &expr) {
    if (dynamic_cast<const BinOpExprNode *>(&expr) || dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        return "(" + exprToString(expr) + ")";
    }
    return exprToString(expr);
}

// Escapes a CFG block label for use inside a Graphviz DOT `label="..."`
// attribute. `\n` is rendered as `\l` so Graphviz left-justifies each line.
std::string escapeDotLabel(const std::string &text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\l"; break;
        default: out += c; break;
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// One-line expression / statement rendering
// ---------------------------------------------------------------------------

std::string exprToString(const ExprNode &expr) {
    if (const auto *intLit = dynamic_cast<const IntLitExprNode *>(&expr)) {
        return std::to_string(intLit->value);
    }
    if (const auto *floatLit = dynamic_cast<const FloatLitExprNode *>(&expr)) {
        std::ostringstream out;
        out << floatLit->value;
        return out.str();
    }
    if (const auto *charLit = dynamic_cast<const CharLitExprNode *>(&expr)) {
        return "'" + escapeChar(charLit->value) + "'";
    }
    if (const auto *stringLit = dynamic_cast<const StringLitExprNode *>(&expr)) {
        return "\"" + escapeString(stringLit->value) + "\"";
    }
    if (const auto *ident = dynamic_cast<const IdentExprNode *>(&expr)) {
        return ident->name;
    }
    if (const auto *unary = dynamic_cast<const UnaryOpExprNode *>(&expr)) {
        return unaryOpSymbol(unary->op) + exprToStringParen(*unary->operand);
    }
    if (const auto *binOp = dynamic_cast<const BinOpExprNode *>(&expr)) {
        return exprToStringParen(*binOp->lhs) + " " + binaryOpSymbol(binOp->op) + " " +
               exprToStringParen(*binOp->rhs);
    }
    if (const auto *call = dynamic_cast<const CallExprNode *>(&expr)) {
        std::string out = call->callee + "(";
        for (std::size_t i = 0; i < call->args.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += exprToString(*call->args[i]);
        }
        out += ")";
        return out;
    }
    return "<expr>";
}

std::string stmtToString(const StmtNode &stmt) {
    if (const auto *decl = dynamic_cast<const VarDeclStmtNode *>(&stmt)) {
        std::string out = typeName(decl->type) + " " + decl->name;
        if (decl->init) {
            out += " = " + exprToString(*decl->init);
        }
        return out;
    }
    if (const auto *assign = dynamic_cast<const AssignStmtNode *>(&stmt)) {
        return assign->name + " = " + exprToString(*assign->value);
    }
    if (const auto *exprStmt = dynamic_cast<const ExprStmtNode *>(&stmt)) {
        return exprToString(*exprStmt->expr);
    }
    if (const auto *ret = dynamic_cast<const ReturnStmtNode *>(&stmt)) {
        return ret->value ? "return " + exprToString(*ret->value) : "return";
    }
    if (dynamic_cast<const BreakStmtNode *>(&stmt)) {
        return "break";
    }
    if (dynamic_cast<const ContinueStmtNode *>(&stmt)) {
        return "continue";
    }
    return "<stmt>";
}

// ---------------------------------------------------------------------------
// CFG
// ---------------------------------------------------------------------------

int CFG::addBlock() {
    CFGBlock block;
    block.id = static_cast<int>(blocks.size());
    blocks.push_back(std::move(block));
    return blocks.back().id;
}

void CFG::addEdge(int from, int to) {
    blocks[from].successors.push_back(to);
    blocks[to].predecessors.push_back(from);
}

void CFG::printDot(std::ostream &out, const std::string &functionName) const {
    out << "  subgraph cluster_" << functionName << " {\n";
    out << "    label=\"" << escapeDotLabel(functionName) << "\";\n";
    out << "    node [shape=box, fontname=\"monospace\", fontsize=10];\n";

    for (const auto &block : blocks) {
        std::string label;
        if (block.id == entry) {
            label += "entry\n";
        }
        if (block.id == exit) {
            label += "exit\n";
        }
        for (const auto *stmt : block.statements) {
            label += stmtToString(*stmt) + "\n";
        }
        if (block.condition) {
            label += "if (" + exprToString(*block.condition) + ")\n";
        }
        if (label.empty()) {
            label = "(empty)\n";
        }

        out << "    \"" << functionName << "_B" << block.id << "\" [label=\""
            << escapeDotLabel(label) << "\"];\n";
    }

    for (const auto &block : blocks) {
        const bool branches = block.condition != nullptr && block.successors.size() == 2;
        for (std::size_t i = 0; i < block.successors.size(); ++i) {
            out << "    \"" << functionName << "_B" << block.id << "\" -> \"" << functionName << "_B"
                << block.successors[i] << "\"";
            if (branches) {
                out << " [label=\"" << (i == 0 ? "true" : "false") << "\"]";
            }
            out << ";\n";
        }
    }

    out << "  }\n";
}

// ---------------------------------------------------------------------------
// CFGBuilder
// ---------------------------------------------------------------------------

CFG CFGBuilder::build(const FuncDefNode &func) {
    cfg_ = CFG();
    loopStack_.clear();

    cfg_.entry = cfg_.addBlock();
    cfg_.exit = cfg_.addBlock();

    const int end = buildBlock(*func.body, cfg_.entry);
    if (end != -1) {
        cfg_.addEdge(end, cfg_.exit);
    }

    return std::move(cfg_);
}

int CFGBuilder::join(const std::vector<int> &branchEnds, int condBlock) {
    if (branchEnds.empty()) {
        return -1;
    }
    if (branchEnds.size() == 1 && branchEnds[0] != condBlock) {
        return branchEnds[0];
    }
    const int merge = cfg_.addBlock();
    for (int b : branchEnds) {
        cfg_.addEdge(b, merge);
    }
    return merge;
}

int CFGBuilder::buildBlock(const BlockStmtNode &block, int current) {
    bool terminated = false;
    for (const auto &stmt : block.statements) {
        if (terminated) {
            // Everything from here on is unreachable. Put it in a fresh
            // block with no predecessors so the unreachable-code check can
            // report it.
            current = cfg_.addBlock();
            terminated = false;
        }
        current = buildStmt(*stmt, current);
        if (current == -1) {
            terminated = true;
        }
    }
    return terminated ? -1 : current;
}

int CFGBuilder::buildStmt(const StmtNode &stmt, int current) {
    if (dynamic_cast<const VarDeclStmtNode *>(&stmt) || dynamic_cast<const AssignStmtNode *>(&stmt) ||
        dynamic_cast<const ExprStmtNode *>(&stmt)) {
        cfg_.blocks[current].statements.push_back(&stmt);
        return current;
    }
    if (dynamic_cast<const ReturnStmtNode *>(&stmt)) {
        cfg_.blocks[current].statements.push_back(&stmt);
        cfg_.addEdge(current, cfg_.exit);
        return -1;
    }
    if (dynamic_cast<const BreakStmtNode *>(&stmt)) {
        cfg_.addEdge(current, loopStack_.back().breakTarget);
        return -1;
    }
    if (dynamic_cast<const ContinueStmtNode *>(&stmt)) {
        cfg_.addEdge(current, loopStack_.back().continueTarget);
        return -1;
    }
    if (const auto *ifStmt = dynamic_cast<const IfStmtNode *>(&stmt)) {
        return buildIf(*ifStmt, current);
    }
    if (const auto *whileStmt = dynamic_cast<const WhileStmtNode *>(&stmt)) {
        return buildWhile(*whileStmt, current);
    }
    if (const auto *forStmt = dynamic_cast<const ForStmtNode *>(&stmt)) {
        return buildFor(*forStmt, current);
    }
    if (const auto *block = dynamic_cast<const BlockStmtNode *>(&stmt)) {
        return buildBlock(*block, current);
    }
    return current;
}

int CFGBuilder::buildIf(const IfStmtNode &stmt, int current) {
    cfg_.blocks[current].condition = stmt.condition.get();

    const int thenBlock = cfg_.addBlock();
    cfg_.addEdge(current, thenBlock);
    const int thenEnd = buildBlock(*stmt.thenBlock, thenBlock);

    std::vector<int> branchEnds;
    if (thenEnd != -1) {
        branchEnds.push_back(thenEnd);
    }

    if (stmt.elseBlock) {
        const int elseBlock = cfg_.addBlock();
        cfg_.addEdge(current, elseBlock);
        const int elseEnd = buildBlock(*stmt.elseBlock, elseBlock);
        if (elseEnd != -1) {
            branchEnds.push_back(elseEnd);
        }
    } else {
        // No else branch: the condition block falls through to the merge
        // block directly when the condition is false.
        branchEnds.push_back(current);
    }

    return join(branchEnds, current);
}

int CFGBuilder::buildWhile(const WhileStmtNode &stmt, int current) {
    const int condBlock = cfg_.addBlock();
    cfg_.addEdge(current, condBlock);
    cfg_.blocks[condBlock].condition = stmt.condition.get();

    const int bodyBlock = cfg_.addBlock();
    const int afterBlock = cfg_.addBlock();
    cfg_.addEdge(condBlock, bodyBlock);
    cfg_.addEdge(condBlock, afterBlock);

    loopStack_.push_back({condBlock, afterBlock});
    const int bodyEnd = buildBlock(*stmt.body, bodyBlock);
    if (bodyEnd != -1) {
        cfg_.addEdge(bodyEnd, condBlock);
    }
    loopStack_.pop_back();

    return afterBlock;
}

int CFGBuilder::buildFor(const ForStmtNode &stmt, int current) {
    if (stmt.init) {
        current = buildStmt(*stmt.init, current);
    }

    const int condBlock = cfg_.addBlock();
    cfg_.addEdge(current, condBlock);

    const int bodyBlock = cfg_.addBlock();
    const int afterBlock = cfg_.addBlock();
    const int updateBlock = cfg_.addBlock();

    if (stmt.condition) {
        cfg_.blocks[condBlock].condition = stmt.condition.get();
        cfg_.addEdge(condBlock, bodyBlock);
        cfg_.addEdge(condBlock, afterBlock);
    } else {
        // `for (;;)` with no condition: the loop only exits via `break`, so
        // `afterBlock` is reachable only through `loopStack_`'s break target.
        cfg_.addEdge(condBlock, bodyBlock);
    }

    // `continue` runs the update expression before re-checking the
    // condition, so it targets `updateBlock` rather than `condBlock`.
    loopStack_.push_back({updateBlock, afterBlock});
    const int bodyEnd = buildBlock(*stmt.body, bodyBlock);
    if (bodyEnd != -1) {
        cfg_.addEdge(bodyEnd, updateBlock);
    }
    loopStack_.pop_back();

    if (stmt.update) {
        buildStmt(*stmt.update, updateBlock);
    }
    cfg_.addEdge(updateBlock, condBlock);

    return afterBlock;
}

} // namespace minic
