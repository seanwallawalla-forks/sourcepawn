// vim: set ts=8 sts=4 sw=4 tw=99 et:
//
//  Copyright (c) AlliedModders LLC 2021
//  Copyright (c) ITB CompuPhase, 1997-2006
//
//  This software is provided "as-is", without any express or implied warranty.
//  In no event will the authors be held liable for any damages arising from
//  the use of this software.
//
//  Permission is granted to anyone to use this software for any purpose,
//  including commercial applications, and to alter it and redistribute it
//  freely, subject to the following restrictions:
//
//  1.  The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software in
//      a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//  2.  Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//  3.  This notice may not be removed or altered from any source distribution.
#pragma once

#include <string>
#include <vector>

#include "data-queue.h"
#include "parse-node.h"
#include "smx-assembly-buffer.h"

class CompileContext;
class ParseTree;
struct symbol;

class CodeGenerator final
{
  public:
    CodeGenerator(CompileContext& cc, ParseTree* tree);

    void Generate();

    void LinkPublicFunction(symbol* sym, uint32_t id);

    const std::vector<std::string>& debug_strings() const { return debug_strings_; }
    const std::vector<symbol*>& native_list() const { return native_list_; }

    const uint8_t* code_ptr() const { return asm_.bytes(); }
    uint32_t code_size() const { return asm_.size(); }
    const uint8_t* data_ptr() const { return data_.dat(); }
    uint32_t data_size() const { return data_.size(); }
    int max_script_memory() const { return max_script_memory_; }

    int DynamicMemorySize() const;

  private:
    // Statements/decls.
    void EmitStmtList(StmtList* list);
    void EmitStmt(Stmt* stmt);
    void EmitChangeScopeNode(ChangeScopeNode* node);
    void EmitVarDecl(VarDecl* decl);
    void EmitPstruct(VarDecl* decl);
    void EmitGlobalVar(VarDecl* decl);
    void EmitLocalVar(VarDecl* decl);
    void EmitIfStmt(IfStmt* stmt);
    void EmitDeleteStmt(DeleteStmt* stmt);
    void EmitDoWhileStmt(DoWhileStmt* stmt);
    void EmitLoopControlStmt(LoopControlStmt* stmt);
    void EmitForStmt(ForStmt* stmt);
    void EmitSwitchStmt(SwitchStmt* stmt);
    void EmitFunctionInfo(FunctionInfo* info);
    void EmitEnumStructDecl(EnumStructDecl* info);
    void EmitMethodmapDecl(MethodmapDecl* info);
    void EmitReturnStmt(ReturnStmt* stmt);
    void EmitReturnArrayStmt(ReturnStmt* stmt);

    // Expressions.
    void EmitExpr(Expr* expr);
    void EmitTest(Expr* expr, bool jump_on_true, sp::Label* target);
    void EmitUnary(UnaryExpr* expr);
    void EmitIncDec(IncDecExpr* expr);
    void EmitBinary(BinaryExpr* expr);
    void EmitBinaryInner(int oper_tok, const UserOperation& in_user_op, Expr* left, Expr* right);
    void EmitLogicalExpr(LogicalExpr* expr);
    void EmitChainedCompareExpr(ChainedCompareExpr* expr);
    void EmitTernaryExpr(TernaryExpr* expr);
    void EmitSymbolExpr(SymbolExpr* expr);
    void EmitIndexExpr(IndexExpr* expr);
    void EmitFieldAccessExpr(FieldAccessExpr* expr);
    void EmitCallExpr(CallExpr* expr);
    void EmitDefaultArgExpr(DefaultArgExpr* expr);
    void EmitCallUserOpExpr(CallUserOpExpr* expr);
    void EmitNewArrayExpr(NewArrayExpr* expr);

    // Logical test helpers.
    bool EmitUnaryExprTest(UnaryExpr* expr, bool jump_on_true, sp::Label* target);
    void EmitLogicalExprTest(LogicalExpr* expr, bool jump_on_true, sp::Label* target);
    bool EmitChainedCompareExprTest(ChainedCompareExpr* expr, bool jump_on_true,
                                    sp::Label* target);

    void EmitDefaultArray(Expr* expr, arginfo* arg);
    void EmitUserOp(const UserOperation& user_op, value* lval);
    void EmitCall(symbol* fun, cell nargs);
    void EmitInc(const value* lval);
    void EmitDec(const value* lval);
    void InvokeGetter(methodmap_method_t* method);
    void InvokeSetter(methodmap_method_t* method, bool save);
    void EmitRvalue(value* lval);
    void EmitStore(const value* lval);
    void EmitBreak();

    void EmitRvalue(const value& lval) {
        value tmp = lval;
        EmitRvalue(&tmp);
    }

    void AddDebugFile(const std::string& line);
    void AddDebugLine(int linenr);
    void AddDebugSymbol(symbol* sym);

    // Helper that automatically handles heap deallocations.
    void EmitExprForStmt(Expr* expr);

  private:
    enum MemuseType {
        MEMUSE_STATIC = 0,
        MEMUSE_DYNAMIC = 1
    };

    struct MemoryUse {
        MemoryUse(MemuseType type, int size)
         : type(type),
           size(size)
        {}
        MemuseType type;
        int size; /* size of array for static (0 for dynamic) */
    };

    struct MemoryScope {
        MemoryScope(MemoryScope&& other)
         : scope_id(other.scope_id),
           usage(std::move(other.usage)),
           needs_restore(other.needs_restore)
        {}
        explicit MemoryScope(int scope_id)
         : scope_id(scope_id),
           needs_restore(false)
        {}
        MemoryScope(const MemoryScope& other) = delete;

        MemoryScope& operator =(const MemoryScope& other) = delete;
        MemoryScope& operator =(MemoryScope&& other) {
            scope_id = other.scope_id;
            usage = std::move(other.usage);
            needs_restore = other.needs_restore;
            return *this;
        }

        int scope_id;
        std::vector<MemoryUse> usage;
        bool needs_restore;
    };

    // Heap functions
    void EnterHeapScope(FlowType flow_type);
    void LeaveHeapScope();
    void TrackTempHeapAlloc(Expr* source, int size);
    void TrackHeapAlloc(MemuseType type, int size);

    // Stack functions
    void pushstacklist();
    void popstacklist(bool codegen);
    int markstack(MemuseType type, int size);
    void modheap_for_scope(const MemoryScope& scope);
    void modstk_for_scope(const MemoryScope& scope);

    // Generates code to free mem usage, but does not pop the list.  
    //  This is used for code like dobreak()/docont()/doreturn().
    // stop_id is the list at which to stop generating.
    void genstackfree(int stop_id);

    int stack_scope_id() { return stack_scopes_.back().scope_id; }
    int heap_scope_id();
    bool has_stack_or_heap_scopes() {
        return !stack_scopes_.empty() || !heap_scopes_.empty();
    }

    void EnterMemoryScope(std::vector<MemoryScope>& frame);
    void AllocInScope(MemoryScope& scope, MemuseType type, int size);
    int PopScope(std::vector<MemoryScope>& scope_list);

  private:
    CompileContext& cc_;
    ParseTree* tree_;
    symbol* func_ = nullptr;
    int max_script_memory_ = 0;

    std::vector<std::string> debug_strings_;
    std::vector<symbol*> native_list_;
    sp::SmxAssemblyBuffer asm_;
    DataQueue data_;

    ke::Maybe<uint32_t> last_break_op_;
    std::vector<MemoryScope> stack_scopes_;
    std::vector<MemoryScope> heap_scopes_;

    // Loop handling.
    struct LoopContext {
        sp::Label break_to;
        sp::Label continue_to;
        int stack_scope_id;
        int heap_scope_id;
    };
    LoopContext* loop_ = nullptr;

    int current_stack_ = 0;
    int current_memory_ = 0;
    int max_func_memory_ = 0;
};
