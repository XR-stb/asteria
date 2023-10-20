// This file is part of Asteria.
// Copyleft 2018 - 2023, LH_Mouse. All wrongs reserved.

#include "../precompiled.ipp"
#include "air_node.hpp"
#include "enums.hpp"
#include "executive_context.hpp"
#include "global_context.hpp"
#include "abstract_hooks.hpp"
#include "analytic_context.hpp"
#include "garbage_collector.hpp"
#include "random_engine.hpp"
#include "runtime_error.hpp"
#include "variable.hpp"
#include "ptc_arguments.hpp"
#include "module_loader.hpp"
#include "air_optimizer.hpp"
#include "../compiler/token_stream.hpp"
#include "../compiler/statement_sequence.hpp"
#include "../compiler/statement.hpp"
#include "../compiler/expression_unit.hpp"
#include "../llds/avmc_queue.hpp"
#include "../utils.hpp"
namespace asteria {
namespace {

void
do_set_rebound(bool& dirty, AIR_Node& res, AIR_Node&& bound)
  {
    dirty = true;
    res = ::std::move(bound);
  }

void
do_rebind_nodes(bool& dirty, cow_vector<AIR_Node>& code, Abstract_Context& ctx)
  {
    for(size_t i = 0;  i < code.size();  ++i)
      if(auto qnode = code.at(i).rebind_opt(ctx))
        do_set_rebound(dirty, code.mut(i), ::std::move(*qnode));
  }

void
do_rebind_nodes(bool& dirty, cow_vector<cow_vector<AIR_Node>>& code, Abstract_Context& ctx)
  {
    for(size_t k = 0;  k < code.size();  ++k)
      for(size_t i = 0;  i < code.at(k).size();  ++i)
        if(auto qnode = code.at(k).at(i).rebind_opt(ctx))
          do_set_rebound(dirty, code.mut(k).mut(i), ::std::move(*qnode));
  }

template<typename NodeT>
opt<AIR_Node>
do_return_rebound_opt(bool dirty, NodeT&& bound)
  {
    opt<AIR_Node> res;
    if(dirty)
      res.emplace(::std::forward<NodeT>(bound));
    return res;
  }

void
do_collect_variables_for_each(Variable_HashMap& staged, Variable_HashMap& temp,
                              const cow_vector<AIR_Node>& code)
  {
    for(size_t i = 0;  i < code.size();  ++i)
      code.at(i).collect_variables(staged, temp);
  }

void
do_collect_variables_for_each(Variable_HashMap& staged, Variable_HashMap& temp,
                              const cow_vector<cow_vector<AIR_Node>>& code)
  {
    for(size_t k = 0;  k < code.size();  ++k)
      for(size_t i = 0;  i < code.at(k).size();  ++i)
        code.at(k).at(i).collect_variables(staged, temp);
  }

void
do_collect_variables_for_each(Variable_HashMap& staged, Variable_HashMap& temp,
                              const cow_vector<AVMC_Queue>& queues)
  {
    for(size_t i = 0;  i < queues.size();  ++i)
      queues.at(i).collect_variables(staged, temp);
  }

using Uparam  = AVMC_Queue::Uparam;
using Header  = AVMC_Queue::Header;

template<typename SparamT>
void
do_avmc_ctor(Header* head, void* arg)
  {
    ::rocket::details_variant::wrapped_move_construct<SparamT>(head->sparam, arg);
  }

template<typename SparamT>
void
do_avmc_dtor(Header* head)
  {
    ::rocket::details_variant::wrapped_destroy<SparamT>(head->sparam);
  }

void
do_solidify_nodes(AVMC_Queue& queue, const cow_vector<AIR_Node>& code)
  {
    queue.clear();

    for(size_t i = 0;  i < code.size();  ++i)
      code.at(i).solidify(queue);

    queue.finalize();
  }

void
do_solidify_nodes(cow_vector<AVMC_Queue>& queues, const cow_vector<cow_vector<AIR_Node>>& code)
  {
    queues.clear();
    queues.append(code.size());

    for(size_t k = 0;  k < code.size();  ++k)
      for(size_t i = 0;  i < code.at(k).size();  ++i)
        code.at(k).at(i).solidify(queues.mut(k));

    for(size_t k = 0;  k < code.size();  ++k)
      queues.mut(k).finalize();
  }

AIR_Status
do_execute_block(const AVMC_Queue& queue, const Executive_Context& ctx)
  {
    Executive_Context ctx_next(Executive_Context::M_plain(), ctx);
    AIR_Status status;
    try {
      status = queue.execute(ctx_next);
    }
    catch(Runtime_Error& except) {
      ctx_next.on_scope_exit_exceptional(except);
      throw;
    }
    ctx_next.on_scope_exit_normal(status);
    return status;
  }

AIR_Status
do_evaluate_subexpression(Executive_Context& ctx, bool assign, const AVMC_Queue& queue)
  {
    if(queue.empty()) {
      // If the queue is empty, leave the condition on the top of the stack.
      return air_status_next;
    }
    else if(assign) {
      // Evaluate the subexpression and assign the result to the first operand.
      // The result value has to be copied, in case that a reference to an element
      // of the LHS operand is returned.
      queue.execute(ctx);
      auto val = ctx.stack().top().dereference_readonly();
      ctx.stack().pop();
      ctx.stack().top().dereference_mutable() = ::std::move(val);
      return air_status_next;
    }
    else {
      // Discard the top which will be overwritten anyway, then evaluate the
      // subexpression. The status code must be forwarded, as PTCs may return
      // `air_status_return_ref`.
      ctx.stack().pop();
      return queue.execute(ctx);
    }
  }

void
do_pop_positional_arguments(Reference_Stack& alt_stack, Reference_Stack& stack, uint32_t count)
  {
    ROCKET_ASSERT(count <= stack.size());
    alt_stack.clear();
    for(uint32_t k = count - 1;  k != UINT32_MAX;  --k)
      alt_stack.push() = ::std::move(stack.mut_top(k));
    stack.pop(count);
  }

AIR_Status
do_invoke_nontail(Reference& self, Global_Context& global,  const Source_Location& sloc,
                 const cow_function& target, Reference_Stack&& stack)
  {
    ASTERIA_CALL_GLOBAL_HOOK(global, on_function_call, sloc, target);
    try {
      target.invoke(self, global, ::std::move(stack));
    }
    catch(Runtime_Error& except) {
      ASTERIA_CALL_GLOBAL_HOOK(global, on_function_except, sloc, target, except);
      throw;
    }
    ASTERIA_CALL_GLOBAL_HOOK(global, on_function_return, sloc, target, self);
    return air_status_next;
  }

AIR_Status
do_invoke_tail(Reference& self, PTC_Aware ptc, const Source_Location& sloc,
              const cow_function& target, Reference_Stack&& stack)
  {
    stack.push() = ::std::move(self);
    self.set_ptc(::rocket::make_refcnt<PTC_Arguments>(sloc, ptc, target, ::std::move(stack)));
    return air_status_return_ref;
  }

template<typename ContainerT>
void
do_duplicate_sequence_common(ContainerT& container, int64_t count)
  {
    if(count < 0)
      ASTERIA_THROW_RUNTIME_ERROR((
          "Negative duplication count (value was `$2`)"),
          count);

    if(container.empty() || (count == 1))
      return;

    if(count == 0) {
      container.clear();
      return;
    }

    // Calculate the result length with overflow checking.
    int64_t rlen;
    if(ROCKET_MUL_OVERFLOW((int64_t) container.size(), count, &rlen) || (rlen > PTRDIFF_MAX))
      ASTERIA_THROW_RUNTIME_ERROR((
          "Data length overflow (`$1` * `$2` > `$3`)"),
          container.size(), count, PTRDIFF_MAX);

    // Duplicate elements, using binary exponential backoff.
    while(container.ssize() < rlen)
      container.append(container.begin(),
          container.begin() + ::rocket::min(rlen - container.ssize(), container.ssize()));
  }

}  // namespace

opt<AIR_Node>
AIR_Node::
rebind_opt(Abstract_Context& ctx) const
  {
    switch(this->index()) {
      case index_clear_stack:
        return nullopt;

      case index_execute_block: {
        const auto& altr = this->m_stor.as<S_execute_block>();

        // Rebind the body in a nested scope.
        bool dirty = false;
        S_execute_block bound = altr;

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_body, ctx_body);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_declare_variable:
      case index_initialize_variable:
        return nullopt;

      case index_if_statement: {
        const auto& altr = this->m_stor.as<S_if_statement>();

        // Rebind both branches in a nested scope.
        bool dirty = false;
        S_if_statement bound = altr;

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_true, ctx_body);
        do_rebind_nodes(dirty, bound.code_false, ctx_body);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_switch_statement: {
        const auto& altr = this->m_stor.as<S_switch_statement>();

        // Rebind all labels and clauses.
        // Labels are to be evaluated in the same scope as the condition
        // expression, and are not parts of the body.
        bool dirty = false;
        S_switch_statement bound = altr;

        do_rebind_nodes(dirty, bound.code_labels, ctx);

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_clauses, ctx_body);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_do_while_statement: {
        const auto& altr = this->m_stor.as<S_do_while_statement>();

        // Rebind the body and the condition expression.
        // The condition expression is not a part of the body.
        bool dirty = false;
        S_do_while_statement bound = altr;

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_body, ctx_body);

        do_rebind_nodes(dirty, bound.code_cond, ctx);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_while_statement: {
        const auto& altr = this->m_stor.as<S_while_statement>();

        // Rebind the condition expression and the body.
        // The condition expression is not a part of the body.
        bool dirty = false;
        S_while_statement bound = altr;

        do_rebind_nodes(dirty, bound.code_cond, ctx);

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_body, ctx_body);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_for_each_statement: {
        const auto& altr = this->m_stor.as<S_for_each_statement>();

        // Rebind the range initializer and the body.
        // The range key and mapped references are declared in a dedicated scope
        // where the initializer is to be evaluated. The body is to be executed
        // in an inner scope, created and destroyed for each iteration.
        bool dirty = false;
        S_for_each_statement bound = altr;

        Analytic_Context ctx_for(Analytic_Context::M_plain(), ctx);
        ctx_for.insert_named_reference(altr.name_key);
        ctx_for.insert_named_reference(altr.name_mapped);
        do_rebind_nodes(dirty, bound.code_init, ctx_for);

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx_for);
        do_rebind_nodes(dirty, bound.code_body, ctx_body);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_for_statement: {
        const auto& altr = this->m_stor.as<S_for_statement>();

        // Rebind the initializer, condition expression and step expression. All
        // these are declared in a dedicated scope where the initializer is to be
        // evaluated. The body is to be executed in an inner scope, created and
        // destroyed for each iteration.
        bool dirty = false;
        S_for_statement bound = altr;

        Analytic_Context ctx_for(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_init, ctx_for);
        do_rebind_nodes(dirty, bound.code_cond, ctx_for);
        do_rebind_nodes(dirty, bound.code_step, ctx_for);

        Analytic_Context ctx_body(Analytic_Context::M_plain(), ctx_for);
        do_rebind_nodes(dirty, bound.code_body, ctx_body);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_try_statement: {
        const auto& altr = this->m_stor.as<S_try_statement>();

        // Rebind the `try` and `catch` clauses.
        bool dirty = false;
        S_try_statement bound = altr;

        Analytic_Context ctx_try(Analytic_Context::M_plain(), ctx);
        do_rebind_nodes(dirty, bound.code_try, ctx_try);

        Analytic_Context ctx_catch(Analytic_Context::M_plain(), ctx);
        ctx_catch.insert_named_reference(altr.name_except);
        do_rebind_nodes(dirty, bound.code_catch, ctx_catch);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_throw_statement:
      case index_assert_statement:
      case index_simple_status:
      case index_check_argument:
      case index_push_global_reference:
        return nullopt;

      case index_push_local_reference: {
        const auto& altr = this->m_stor.as<S_push_local_reference>();

        // Get the context.
        auto qctx = static_cast<const Abstract_Context*>(&ctx);
        for(uint32_t k = 0;  k != altr.depth;  ++k)
          qctx = qctx->get_parent_opt();

        if(qctx->is_analytic())
          return nullopt;

        // Look for the name.
        auto qref = qctx->get_named_reference_opt(altr.name);
        if(!qref)
          return nullopt;
        else if(qref->is_invalid())
          ASTERIA_THROW_RUNTIME_ERROR((
              "Initialization of variable or reference `$1` bypassed"),
              altr.name);

        // Bind this reference.
        S_push_bound_reference xnode = { *qref };
        return ::std::move(xnode);
      }

      case index_push_bound_reference:
        return nullopt;

      case index_define_function: {
        const auto& altr = this->m_stor.as<S_define_function>();

        // Rebind the function body.
        // This is the only scenario where names in the outer scope are visible
        // to the body of a function.
        bool dirty = false;
        S_define_function bound = altr;

        Analytic_Context ctx_func(Analytic_Context::M_function(), &ctx, altr.params);
        do_rebind_nodes(dirty, bound.code_body, ctx_func);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_branch_expression: {
        const auto& altr = this->m_stor.as<S_branch_expression>();

        // Rebind both branches.
        bool dirty = false;
        S_branch_expression bound = altr;

        do_rebind_nodes(dirty, bound.code_true, ctx);
        do_rebind_nodes(dirty, bound.code_false, ctx);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_function_call:
      case index_push_unnamed_array:
      case index_push_unnamed_object:
      case index_apply_operator:
      case index_unpack_struct_array:
      case index_unpack_struct_object:
      case index_define_null_variable:
      case index_single_step_trap:
      case index_variadic_call:
        return nullopt;

      case index_defer_expression: {
        const auto& altr = this->m_stor.as<S_defer_expression>();

        // Rebind the expression.
        bool dirty = false;
        S_defer_expression bound = altr;

        do_rebind_nodes(dirty, bound.code_body, ctx);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_import_call:
      case index_declare_reference:
      case index_initialize_reference:
        return nullopt;

      case index_catch_expression: {
        const auto& altr = this->m_stor.as<S_catch_expression>();

        // Rebind the expression.
        bool dirty = false;
        S_catch_expression bound = altr;

        do_rebind_nodes(dirty, bound.code_body, ctx);

        return do_return_rebound_opt(dirty, ::std::move(bound));
      }

      case index_return_statement:
      case index_push_constant:
      case index_push_constant_int48:
        return nullopt;

      default:
        ASTERIA_TERMINATE(("Invalid AIR node type (index `$1`)"), this->index());
    }
  }

void
AIR_Node::
collect_variables(Variable_HashMap& staged, Variable_HashMap& temp) const
  {
    switch(this->index()) {
      case index_clear_stack:
        return;

      case index_execute_block: {
        const auto& altr = this->m_stor.as<S_execute_block>();

        // Collect variables from the body.
        do_collect_variables_for_each(staged, temp, altr.code_body);
        return;
      }

      case index_declare_variable:
      case index_initialize_variable:
        return;

      case index_if_statement: {
        const auto& altr = this->m_stor.as<S_if_statement>();

        // Collect variables from both branches.
        do_collect_variables_for_each(staged, temp, altr.code_true);
        do_collect_variables_for_each(staged, temp, altr.code_false);
        return;
      }

      case index_switch_statement: {
        const auto& altr = this->m_stor.as<S_switch_statement>();

        // Collect variables from all labels and clauses.
        do_collect_variables_for_each(staged, temp, altr.code_labels);
        do_collect_variables_for_each(staged, temp, altr.code_clauses);
        return;
      }

      case index_do_while_statement: {
        const auto& altr = this->m_stor.as<S_do_while_statement>();

        // Collect variables from the body and the condition expression.
        do_collect_variables_for_each(staged, temp, altr.code_body);
        do_collect_variables_for_each(staged, temp, altr.code_cond);
        return;
      }

      case index_while_statement: {
        const auto& altr = this->m_stor.as<S_while_statement>();

        // Collect variables from the condition expression and the body.
        do_collect_variables_for_each(staged, temp, altr.code_cond);
        do_collect_variables_for_each(staged, temp, altr.code_body);
        return;
      }

      case index_for_each_statement: {
        const auto& altr = this->m_stor.as<S_for_each_statement>();

        // Collect variables from the range initializer and the body.
        do_collect_variables_for_each(staged, temp, altr.code_init);
        do_collect_variables_for_each(staged, temp, altr.code_body);
        return;
      }

      case index_for_statement: {
        const auto& altr = this->m_stor.as<S_for_statement>();

        // Collect variables from the initializer, condition expression and
        // step expression.
        do_collect_variables_for_each(staged, temp, altr.code_init);
        do_collect_variables_for_each(staged, temp, altr.code_cond);
        do_collect_variables_for_each(staged, temp, altr.code_step);
        return;
      }

      case index_try_statement: {
        const auto& altr = this->m_stor.as<S_try_statement>();

        // Collect variables from the `try` and `catch` clauses.
        do_collect_variables_for_each(staged, temp, altr.code_try);
        do_collect_variables_for_each(staged, temp, altr.code_catch);
        return;
      }

      case index_throw_statement:
      case index_assert_statement:
      case index_simple_status:
      case index_check_argument:
      case index_push_global_reference:
      case index_push_local_reference:
        return;

      case index_push_bound_reference: {
        const auto& altr = this->m_stor.as<S_push_bound_reference>();

        // Collect variables from the bound reference.
        altr.ref.collect_variables(staged, temp);
        return;
      }

      case index_define_function: {
        const auto& altr = this->m_stor.as<S_define_function>();

        // Collect variables from the function body.
        do_collect_variables_for_each(staged, temp, altr.code_body);
        return;
      }

      case index_branch_expression: {
        const auto& altr = this->m_stor.as<S_branch_expression>();

        // Collect variables from both branches.
        do_collect_variables_for_each(staged, temp, altr.code_true);
        do_collect_variables_for_each(staged, temp, altr.code_false);
        return;
      }

      case index_function_call:
      case index_push_unnamed_array:
      case index_push_unnamed_object:
      case index_apply_operator:
      case index_unpack_struct_array:
      case index_unpack_struct_object:
      case index_define_null_variable:
      case index_single_step_trap:
      case index_variadic_call:
        return;

      case index_defer_expression: {
        const auto& altr = this->m_stor.as<S_defer_expression>();

        // Collect variables from the expression.
        do_collect_variables_for_each(staged, temp, altr.code_body);
        return;
      }

      case index_import_call:
      case index_declare_reference:
      case index_initialize_reference:
        return;

      case index_catch_expression: {
        const auto& altr = this->m_stor.as<S_catch_expression>();

        // Collect variables from the expression.
        do_collect_variables_for_each(staged, temp, altr.code_body);
        return;
      }

      case index_return_statement:
      case index_push_constant:
      case index_push_constant_int48:
        return;

      default:
        ASTERIA_TERMINATE(("Invalid AIR node type (index `$1`)"), this->index());
    }
  }

void
AIR_Node::
solidify(AVMC_Queue& queue) const
  {
    switch(this->index()) {
      case index_clear_stack: {
        const auto& altr = this->m_stor.as<S_clear_stack>();

        (void) altr;

        queue.append(
          +[](Executive_Context& ctx, const Header* /*head*/) ROCKET_FLATTEN -> AIR_Status
          {
            ctx.stack().clear();
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      case index_execute_block: {
        const auto& altr = this->m_stor.as<S_execute_block>();

        struct Sparam
          {
            AVMC_Queue queue_body;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queue_body, altr.code_body);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Execute the block on a new context. The block may contain control
            // statements, so the status shall be forwarded verbatim.
            return do_execute_block(sp.queue_body, ctx);
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_body.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_declare_variable: {
        const auto& altr = this->m_stor.as<S_declare_variable>();

        struct Sparam
          {
            phsh_string name;
          };

        Sparam sp2;
        sp2.name = altr.name;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            const auto& sloc = head->pv_meta->sloc;

            // Allocate a variable and inject it into the current context.
            const auto gcoll = ctx.global().garbage_collector();
            const auto var = gcoll->create_variable();
            ctx.insert_named_reference(sp.name).set_variable(var);
            ASTERIA_CALL_GLOBAL_HOOK(ctx.global(), on_variable_declare, sloc, sp.name);

            // Push a copy of the reference onto the stack, which we will get
            // back after the initializer finishes execution.
            ctx.stack().push().set_variable(var);
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_initialize_variable: {
        const auto& altr = this->m_stor.as<S_initialize_variable>();

        Uparam up2;
        up2.b0 = altr.immutable;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            // Read the value of the initializer. The initializer must not have
            // been empty for this function.
            const auto& val = ctx.stack().top().dereference_readonly();
            ctx.stack().pop();

            // Get the variable back.
            auto var = ctx.stack().top().unphase_variable_opt();
            ctx.stack().pop();
            ROCKET_ASSERT(var && !var->is_initialized());

            // Initialize it with this value.
            var->initialize(val);
            var->set_immutable(up.b0);
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_if_statement: {
        const auto& altr = this->m_stor.as<S_if_statement>();

        Uparam up2;
        up2.b0 = altr.negative;

        struct Sparam
          {
            AVMC_Queue queue_true;
            AVMC_Queue queue_false;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queue_true, altr.code_true);
        do_solidify_nodes(sp2.queue_false, altr.code_false);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Read the condition and execute the corresponding branch as a block.
            return (ctx.stack().top().dereference_readonly().test() != up.b0)
                      ? do_execute_block(sp.queue_true, ctx)
                      : do_execute_block(sp.queue_false, ctx);
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_true.collect_variables(staged, temp);
            sp.queue_false.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_switch_statement: {
        const auto& altr = this->m_stor.as<S_switch_statement>();

        struct Sparam
          {
            cow_vector<AVMC_Queue> queues_labels;
            cow_vector<AVMC_Queue> queues_clauses;
            cow_vector<cow_vector<phsh_string>> names_added;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queues_labels, altr.code_labels);
        do_solidify_nodes(sp2.queues_clauses, altr.code_clauses);
        sp2.names_added = altr.names_added;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Get the number of clauses.
            size_t nclauses = sp.queues_labels.size();
            ROCKET_ASSERT(nclauses == sp.queues_clauses.size());
            ROCKET_ASSERT(nclauses == sp.names_added.size());

            // Read the value of the condition and find the target clause for it.
            auto cond = ctx.stack().top().dereference_readonly();
            size_t target_index = SIZE_MAX;

            // This is different from the `switch` statement in C, where `case` labels must
            // have constant operands.
            for(size_t i = 0;  i < nclauses;  ++i) {
              // This is a `default` clause if the condition is empty, and a `case` clause
              // otherwise.
              if(sp.queues_labels.at(i).empty()) {
                target_index = i;
                continue;
              }

              // Evaluate the operand and check whether it equals `cond`.
              AIR_Status status = sp.queues_labels.at(i).execute(ctx);
              ROCKET_ASSERT(status == air_status_next);
              if(ctx.stack().top().dereference_readonly().compare_partial(cond) == compare_equal) {
                target_index = i;
                break;
              }
            }

            if(target_index >= nclauses)
              return air_status_next;

            // Skip this statement if no matching clause has been found.
            Executive_Context ctx_body(Executive_Context::M_plain(), ctx);
            AIR_Status status = air_status_next;
            try {
              for(size_t i = 0;  i < nclauses;  ++i)
                if(i < target_index) {
                  // Inject bypassed variables into the scope.
                  for(const auto& name : sp.names_added.at(i))
                    ctx_body.insert_named_reference(name);
                }
                else {
                  // Execute the body of this clause.
                  status = sp.queues_clauses.at(i).execute(ctx_body);
                  if(::rocket::is_any_of(status, { air_status_break_unspec, air_status_break_switch })) {
                    status = air_status_next;
                    break;
                  }
                  else if(status != air_status_next)
                    break;
                }
            }
            catch(Runtime_Error& except) {
              ctx_body.on_scope_exit_exceptional(except);
              throw;
            }
            ctx_body.on_scope_exit_normal(status);
            return status;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            do_collect_variables_for_each(staged, temp, sp.queues_labels);
            do_collect_variables_for_each(staged, temp, sp.queues_clauses);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_do_while_statement: {
        const auto& altr = this->m_stor.as<S_do_while_statement>();

        Uparam up2;
        up2.b0 = altr.negative;

        struct Sparam
          {
            AVMC_Queue queues_body;
            AVMC_Queue queues_cond;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queues_body, altr.code_body);
        do_solidify_nodes(sp2.queues_cond, altr.code_cond);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // This is identical to C.
            AIR_Status status = air_status_next;
            for(;;) {
              // Execute the body.
              status = do_execute_block(sp.queues_body, ctx);
              if(::rocket::is_any_of(status, { air_status_break_unspec, air_status_break_while })) {
                status = air_status_next;
                break;
              }
              else if(::rocket::is_none_of(status, { air_status_next, air_status_continue_unspec,
                                                     air_status_continue_while }))
                break;

              // Check the condition.
              status = sp.queues_cond.execute(ctx);
              ROCKET_ASSERT(status == air_status_next);
              if(ctx.stack().top().dereference_readonly().test() == up.b0)
                break;
            }
            return status;
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queues_body.collect_variables(staged, temp);
            sp.queues_cond.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_while_statement: {
        const auto& altr = this->m_stor.as<S_while_statement>();

        Uparam up2;
        up2.b0 = altr.negative;

        struct Sparam
          {
            AVMC_Queue queues_cond;
            AVMC_Queue queues_body;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queues_cond, altr.code_cond);
        do_solidify_nodes(sp2.queues_body, altr.code_body);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // This is identical to C.
            AIR_Status status = air_status_next;
            for(;;) {
              // Check the condition.
              status = sp.queues_cond.execute(ctx);
              ROCKET_ASSERT(status == air_status_next);
              if(ctx.stack().top().dereference_readonly().test() == up.b0)
                break;

              // Execute the body.
              status = do_execute_block(sp.queues_body, ctx);
              if(::rocket::is_any_of(status, { air_status_break_unspec, air_status_break_while })) {
                status = air_status_next;
                break;
              }
              else if(::rocket::is_none_of(status, { air_status_next, air_status_continue_unspec,
                                                     air_status_continue_while }))
                break;
            }
            return status;
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queues_cond.collect_variables(staged, temp);
            sp.queues_body.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_for_each_statement: {
        const auto& altr = this->m_stor.as<S_for_each_statement>();

        struct Sparam
          {
            phsh_string name_key;
            phsh_string name_mapped;
            Source_Location sloc_init;
            AVMC_Queue queue_init;
            AVMC_Queue queue_body;
          };

        Sparam sp2;
        sp2.name_key = altr.name_key;
        sp2.name_mapped = altr.name_mapped;
        sp2.sloc_init = altr.sloc_init;
        do_solidify_nodes(sp2.queue_init, altr.code_init);
        do_solidify_nodes(sp2.queue_body, altr.code_body);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // We have to create an outer context due to the fact that the key
            // and mapped references outlast every iteration.
            Executive_Context ctx_for(Executive_Context::M_plain(), ctx);

            // Create key and mapped references.
            auto& key = ctx_for.insert_named_reference(sp.name_key);
            auto& mapped = ctx_for.insert_named_reference(sp.name_mapped);
            refcnt_ptr<Variable> kvar;

            // Evaluate the range initializer and set the range up, which isn't
            // going to change for all loops.
            AIR_Status status = sp.queue_init.execute(ctx_for);
            ROCKET_ASSERT(status == air_status_next);
            mapped = ::std::move(ctx_for.stack().mut_top());

            const auto range = mapped.dereference_readonly();
            if(range.is_null()) {
              // Do nothing.
              return air_status_next;
            }
            else if(range.is_array()) {
              const auto& arr = range.as_array();
              for(int64_t i = 0;  i < arr.ssize();  ++i) {
                // Set the key variable which is the subscript of the mapped
                // element in the array.
                if(!kvar) {
                  kvar = ctx.global().garbage_collector()->create_variable();
                  key.set_variable(kvar);
                }
                else
                  mapped.pop_modifier();

                kvar->initialize(i);
                kvar->set_immutable();

                Reference_Modifier::S_array_index xmod = { i };
                mapped.push_modifier(::std::move(xmod));
                mapped.dereference_readonly();

                // Execute the loop body.
                status = do_execute_block(sp.queue_body, ctx_for);
                if(::rocket::is_any_of(status, { air_status_break_unspec, air_status_break_for })) {
                  status = air_status_next;
                  break;
                }
                else if(::rocket::is_none_of(status, { air_status_next, air_status_continue_unspec,
                                                       air_status_continue_for }))
                  break;
              }
              return status;
            }
            else if(range.is_object()) {
              const auto& obj = range.as_object();
              for(auto it = obj.begin();  it != obj.end();  ++it) {
                // Set the key variable which is the name of the mapped element
                // in the object.
                if(!kvar) {
                  kvar = ctx.global().garbage_collector()->create_variable();
                  key.set_variable(kvar);
                }
                else
                  mapped.pop_modifier();

                kvar->initialize(it->first.rdstr());
                kvar->set_immutable();

                Reference_Modifier::S_object_key xmod = { it->first };
                mapped.push_modifier(::std::move(xmod));
                mapped.dereference_readonly();

                // Execute the loop body.
                status = do_execute_block(sp.queue_body, ctx_for);
                if(::rocket::is_any_of(status, { air_status_break_unspec, air_status_break_for })) {
                  status = air_status_next;
                  break;
                }
                else if(::rocket::is_none_of(status, { air_status_next, air_status_continue_unspec,
                                                       air_status_continue_for }))
                  break;

                // Restore the mapped reference.
                mapped.pop_modifier();
              }
              return status;
            }
            else
              throw Runtime_Error(Runtime_Error::M_throw(),
                        format_string("Range value not iterable (value `$1`)", range),
                        sp.sloc_init);
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_init.collect_variables(staged, temp);
            sp.queue_body.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_for_statement: {
        const auto& altr = this->m_stor.as<S_for_statement>();

        struct Sparam
          {
            AVMC_Queue queue_init;
            AVMC_Queue queue_cond;
            AVMC_Queue queue_step;
            AVMC_Queue queue_body;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queue_init, altr.code_init);
        do_solidify_nodes(sp2.queue_cond, altr.code_cond);
        do_solidify_nodes(sp2.queue_step, altr.code_step);
        do_solidify_nodes(sp2.queue_body, altr.code_body);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // This is the same as the `for` statement in C. We have to create
            // an outer context due to the fact that names declared in the first
            // segment outlast every iteration.
            Executive_Context ctx_for(Executive_Context::M_plain(), ctx);

            // Execute the loop initializer, which shall only be a definition or
            // an expression statement.
            AIR_Status status = sp.queue_init.execute(ctx_for);
            ROCKET_ASSERT(status == air_status_next);
            for(;;) {
              // Check the condition. There is a special case: If the condition
              // is empty then the loop is infinite.
              status = sp.queue_cond.execute(ctx_for);
              ROCKET_ASSERT(status == air_status_next);
              if(!ctx_for.stack().empty() && !ctx_for.stack().top().dereference_readonly().test())
                break;

              // Execute the body.
              status = do_execute_block(sp.queue_body, ctx_for);
              if(::rocket::is_any_of(status, { air_status_break_unspec, air_status_break_for })) {
                status = air_status_next;
                break;
              }
              else if(::rocket::is_none_of(status, { air_status_next, air_status_continue_unspec,
                                                     air_status_continue_for }))
                break;

              // Execute the increment.
              status = sp.queue_step.execute(ctx_for);
              ROCKET_ASSERT(status == air_status_next);
            }
            return status;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_init.collect_variables(staged, temp);
            sp.queue_cond.collect_variables(staged, temp);
            sp.queue_step.collect_variables(staged, temp);
            sp.queue_body.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_try_statement: {
        const auto& altr = this->m_stor.as<S_try_statement>();

        struct Sparam
          {
            AVMC_Queue queue_try;
            Source_Location sloc_catch;
            phsh_string name_except;
            AVMC_Queue queue_catch;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queue_try, altr.code_try);
        sp2.sloc_catch = altr.sloc_catch;
        sp2.name_except = altr.name_except;
        do_solidify_nodes(sp2.queue_catch, altr.code_catch);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // This is almost identical to JavaScript but not to C++. Only one
            // `catch` clause is allowed.
            AIR_Status status;
            try {
              // Execute the `try` block. If no exception is thrown, this will
              // have little overhead.
              status = do_execute_block(sp.queue_try, ctx);
              if(status == air_status_return_ref)
                ctx.stack().mut_top().check_function_result(ctx.global());
              return status;
            }
            catch(Runtime_Error& except) {
              // Append a frame due to exit of the `try` clause.
              // Reuse the exception object. Don't bother allocating a new one.
              except.push_frame_try(head->pv_meta->sloc);

              // This branch must be executed inside this `catch` block.
              // User-provided bindings may obtain the current exception using
              // `::std::current_exception`.
              Executive_Context ctx_catch(Executive_Context::M_plain(), ctx);
              try {
                // Set the exception reference.
                ctx_catch.insert_named_reference(sp.name_except)
                    .set_temporary(except.value());

                // Set backtrace frames.
                V_array backtrace;
                for(size_t k = 0;  k < except.count_frames();  ++k) {
                  V_object r;
                  r.try_emplace(sref("frame"), sref(except.frame(k).what_type()));
                  r.try_emplace(sref("file"), except.frame(k).file());
                  r.try_emplace(sref("line"), except.frame(k).line());
                  r.try_emplace(sref("column"), except.frame(k).column());
                  r.try_emplace(sref("value"), except.frame(k).value());
                  backtrace.emplace_back(::std::move(r));
                }

                ctx_catch.insert_named_reference(sref("__backtrace"))
                    .set_temporary(::std::move(backtrace));

                // Execute the `catch` clause.
                status = sp.queue_catch.execute(ctx_catch);
              }
              catch(Runtime_Error& nested) {
                ctx_catch.on_scope_exit_exceptional(nested);
                nested.push_frame_catch(sp.sloc_catch, except.value());
                throw;
              }
              ctx_catch.on_scope_exit_normal(status);
              return status;
            }
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_try.collect_variables(staged, temp);
            sp.queue_catch.collect_variables(staged, temp);
          }

          // Symbols
          , &(altr.sloc_try)
        );
        return;
      }

      case index_throw_statement: {
        const auto& altr = this->m_stor.as<S_throw_statement>();

        struct Sparam
          {
            Source_Location sloc;
          };

        Sparam sp2;
        sp2.sloc = altr.sloc;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Read a value and throw it. The operand expression must not have
            // been empty fof this function.
            const auto& val = ctx.stack().top().dereference_readonly();
            ctx.stack().pop();

            throw Runtime_Error(Runtime_Error::M_throw(), val, sp.sloc);
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      case index_assert_statement: {
        const auto& altr = this->m_stor.as<S_assert_statement>();

        struct Sparam
          {
            Source_Location sloc;
            cow_string msg;
          };

        Sparam sp2;
        sp2.sloc = altr.sloc;
        sp2.msg = altr.msg;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Check the operand.
            const auto& val = ctx.stack().top().dereference_readonly();
            ctx.stack().pop();

            // Throw an exception if the assertion fails. This cannot be disabled.
            if(!val.test())
              throw Runtime_Error(Runtime_Error::M_assert(), sp.sloc, sp.msg);

            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      case index_simple_status: {
        const auto& altr = this->m_stor.as<S_simple_status>();

        Uparam up2;
        up2.u0 = altr.status;

        queue.append(
          +[](Executive_Context& /*ctx*/, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            return static_cast<AIR_Status>(head->uparam.u0);
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      case index_check_argument: {
        const auto& altr = this->m_stor.as<S_check_argument>();

        Uparam up2;
        up2.b0 = altr.by_ref;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            if(up.b0) {
              // The argument is passed by reference, so check whether it is
              // dereferenceable.
              ctx.stack().top().dereference_readonly();
              return air_status_next;
            }
            else {
              // The argument is passed by copy, so convert it to a temporary.
              ctx.stack().mut_top().dereference_copy();
              return air_status_next;
            }
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_push_global_reference: {
        const auto& altr = this->m_stor.as<S_push_global_reference>();

        struct Sparam
          {
            phsh_string name;
          };

        Sparam sp2;
        sp2.name = altr.name;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Look for the name in the global context.
            auto qref = ctx.global().get_named_reference_opt(sp.name);
            if(!qref)
              ASTERIA_THROW_RUNTIME_ERROR(("Undeclared identifier `$1`"), sp.name);
            else if(qref->is_invalid())
              ASTERIA_THROW_RUNTIME_ERROR(("Reference `$1` not initialized"), sp.name);

            // Push a copy of the reference onto the stack.
            ctx.stack().push() = *qref;
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_push_local_reference: {
        const auto& altr = this->m_stor.as<S_push_local_reference>();

        Uparam up2;
        up2.u2345 = altr.depth;

        struct Sparam
          {
            phsh_string name;
          };

        Sparam sp2;
        sp2.name = altr.name;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Locate the target context.
            const Executive_Context* ctx_at_depth = &ctx;
            for(uint32_t k = 0;  k != up.u2345;  ++k)
              ctx_at_depth = ctx_at_depth->get_parent_opt();

            // Look for the name in the target context.
            auto qref = ctx_at_depth->get_named_reference_opt(sp.name);
            if(!qref)
              ASTERIA_THROW_RUNTIME_ERROR(("Undeclared identifier `$1`"), sp.name);
            else if(qref->is_invalid())
              ASTERIA_THROW_RUNTIME_ERROR(("Reference `$1` not initialized"), sp.name);

            // Push a copy of the reference onto the stack.
            ctx.stack().push() = *qref;
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_push_bound_reference: {
        const auto& altr = this->m_stor.as<S_push_bound_reference>();

        struct Sparam
          {
            Reference ref;
          };

        Sparam sp2;
        sp2.ref = altr.ref;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            ctx.stack().push() = sp.ref;
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.ref.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_define_function: {
        const auto& altr = this->m_stor.as<S_define_function>();

        struct Sparam
          {
            Compiler_Options opts;
            cow_string func;
            cow_vector<phsh_string> params;
            cow_vector<AIR_Node> code_body;
          };

        Sparam sp2;
        sp2.opts = altr.opts;
        sp2.func = altr.func;
        sp2.params = altr.params;
        sp2.code_body = altr.code_body;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            const auto& sloc = head->pv_meta->sloc;

            // Instantiate the function.
            AIR_Optimizer optmz(sp.opts);
            optmz.rebind(&ctx, sp.params, sp.code_body);
            auto target = optmz.create_function(sloc, sp.func);

            // Push the function as a temporary value.
            ctx.stack().push().set_temporary(::std::move(target));
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            do_collect_variables_for_each(staged, temp, sp.code_body);
          }

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_branch_expression: {
        const auto& altr = this->m_stor.as<S_branch_expression>();

        Uparam up2;
        up2.b0 = altr.assign;
        up2.b1 = altr.coalescence;

        struct Sparam
          {
            AVMC_Queue queue_true;
            AVMC_Queue queue_false;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queue_true, altr.code_true);
        do_solidify_nodes(sp2.queue_false, altr.code_false);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Read the condition and evaluate the corresponding subexpression.
            return (up.b1 ? ctx.stack().top().dereference_readonly().is_null()
                          : ctx.stack().top().dereference_readonly().test())
                     ? do_evaluate_subexpression(ctx, up.b0, sp.queue_true)
                     : do_evaluate_subexpression(ctx, up.b0, sp.queue_false);
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_true.collect_variables(staged, temp);
            sp.queue_false.collect_variables(staged, temp);
          }

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_function_call: {
        const auto& altr = this->m_stor.as<S_function_call>();

        Uparam up2;
        up2.u0 = altr.ptc;
        up2.u2345 = altr.nargs;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sloc = head->pv_meta->sloc;

            // Pop arguments off the stack from right to left.
            const auto sentry = ctx.global().copy_recursion_sentry();
            ASTERIA_CALL_GLOBAL_HOOK(ctx.global(), on_single_step_trap, sloc);

            auto& alt_stack = ctx.alt_stack();
            auto& stack = ctx.stack();
            do_pop_positional_arguments(alt_stack, stack, up.u2345);

            // Copy the target, which shall be of type `function`.
            auto val = stack.top().dereference_readonly();
            if(val.is_null())
              ASTERIA_THROW_RUNTIME_ERROR(("Function not found"));
            else if(!val.is_function())
              ASTERIA_THROW_RUNTIME_ERROR(("Attempt to call a non-function (value `$1`)"), val);

            const auto& target = val.as_function();
            auto& self = stack.mut_top().pop_modifier();
            stack.clear_cache();
            alt_stack.clear_cache();

            return ROCKET_EXPECT(up.u0 == ptc_aware_none)
                     ? do_invoke_nontail(self, ctx.global(), sloc, target, ::std::move(alt_stack))
                     : do_invoke_tail(self, static_cast<PTC_Aware>(up.u0), sloc, target, ::std::move(alt_stack));
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_push_unnamed_array: {
        const auto& altr = this->m_stor.as<S_push_unnamed_array>();

        Uparam up2;
        up2.u2345 = altr.nelems;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            // Pop elements from the stack and fill them from right to left.
            V_array arr;
            arr.resize(up.u2345);
            for(auto it = arr.mut_rbegin();  it != arr.rend();  ++it) {
              *it = ctx.stack().top().dereference_readonly();
              ctx.stack().pop();
            }

            // Push the array as a temporary.
            ctx.stack().push().set_temporary(::std::move(arr));
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_push_unnamed_object: {
        const auto& altr = this->m_stor.as<S_push_unnamed_object>();

        struct Sparam
          {
            cow_vector<phsh_string> keys;
          };

        Sparam sp2;
        sp2.keys = altr.keys;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Pop elements from the stack and set them from right to left. In case
            // of duplicate keys, the rightmost value takes precedence.
            V_object obj;
            obj.reserve(sp.keys.size());
            for(auto it = sp.keys.rbegin();  it != sp.keys.rend();  ++it) {
              obj.try_emplace(*it, ctx.stack().top().dereference_readonly());
              ctx.stack().pop();
            }

            // Push the object as a temporary.
            ctx.stack().push().set_temporary(::std::move(obj));
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_apply_operator: {
        const auto& altr = this->m_stor.as<S_apply_operator>();

        Uparam up2;
        up2.b0 = altr.assign;
        up2.u1 = altr.xop;

        switch(altr.xop) {
          case xop_inc:
          case xop_dec:
          case xop_unset:
          case xop_head:
          case xop_tail:
          case xop_random:
            // unary
            queue.append(
              +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
              {
                const auto& up = head->uparam;
                auto& top = ctx.stack().mut_top();

                switch(up.u1) {
                  case xop_inc: {
                    // `assign` is `true` for the postfix variant and `false` for
                    // the prefix variant.
                    auto& rhs = top.dereference_mutable();

                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      // Increment the value with overflow checking.
                      int64_t result;
                      if(ROCKET_ADD_OVERFLOW(val, 1, &result))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer increment overflow (operand was `$1`)"),
                            val);

                      if(up.b0)
                        top.set_temporary(val);

                      val = result;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      V_real& val = rhs.mut_real();

                      // Overflow will result in an infinity, so this is safe.
                      double result = val + 1;

                      if(up.b0)
                        top.set_temporary(val);

                      val = result;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Increment not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_dec: {
                    // `assign` is `true` for the postfix variant and `false` for
                    // the prefix variant.
                    auto& rhs = top.dereference_mutable();

                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      // Decrement the value with overflow checking.
                      int64_t result;
                      if(ROCKET_SUB_OVERFLOW(val, 1, &result))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer decrement overflow (operand was `$1`)"),
                            val);

                      if(up.b0)
                        top.set_temporary(val);

                      val = result;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      V_real& val = rhs.mut_real();

                      // Overflow will result in an infinity, so this is safe.
                      double result = val - 1;

                      if(up.b0)
                        top.set_temporary(val);

                      val = result;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Decrement not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_unset: {
                    // Unset the last element and return it as a temporary.
                    // `assign` is ignored.
                    auto val = top.dereference_unset();
                    top.set_temporary(::std::move(val));
                    return air_status_next;
                  }

                  case xop_head: {
                    // Push an array head modifier. `assign` is ignored.
                    Reference_Modifier::S_array_head xmod = { };
                    top.push_modifier(::std::move(xmod));
                    top.dereference_readonly();
                    return air_status_next;
                  }

                  case xop_tail: {
                    // Push an array tail modifier. `assign` is ignored.
                    Reference_Modifier::S_array_tail xmod = { };
                    top.push_modifier(::std::move(xmod));
                    top.dereference_readonly();
                    return air_status_next;
                  }

                  case xop_random: {
                    // Push a random subscript.
                    uint32_t seed = ctx.global().random_engine()->bump();
                    Reference_Modifier::S_array_random xmod = { seed };
                    top.push_modifier(::std::move(xmod));
                    top.dereference_readonly();
                    return air_status_next;
                  }

                  default:
                    ROCKET_UNREACHABLE();
                }
              }

              // Uparam
              , up2

              // Sparam
              // (none)

              // Collector
              // (none)

              // Symbols
              , &(altr.sloc)
            );
            return;

          case xop_assign:
          case xop_index:
            // binary
            queue.append(
              +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
              {
                const auto& up = head->uparam;
                auto& rhs = ctx.stack().mut_top().dereference_copy();
                ctx.stack().pop();
                auto& top = ctx.stack().mut_top();

                switch(up.u1) {
                  case xop_assign: {
                    // `assign` is ignored.
                    top.dereference_mutable() = ::std::move(rhs);
                    return air_status_next;
                  }

                  case xop_index: {
                    // Push a subscript.
                    if(rhs.type() == type_integer) {
                      Reference_Modifier::S_array_index xmod = { rhs.as_integer() };
                      top.push_modifier(::std::move(xmod));
                      top.dereference_readonly();
                      return air_status_next;
                    }
                    else if(rhs.type() == type_string) {
                      Reference_Modifier::S_object_key xmod = { rhs.as_string() };
                      top.push_modifier(::std::move(xmod));
                      top.dereference_readonly();
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Subscript value not valid (operand was `$1`)"),
                          rhs);
                  }

                  default:
                    ROCKET_UNREACHABLE();
                }
              }

              // Uparam
              , up2

              // Sparam
              // (none)

              // Collector
              // (none)

              // Symbols
              , &(altr.sloc)
            );
            return;

          case xop_pos:
          case xop_neg:
          case xop_notb:
          case xop_notl:
          case xop_countof:
          case xop_typeof:
          case xop_sqrt:
          case xop_isnan:
          case xop_isinf:
          case xop_abs:
          case xop_sign:
          case xop_round:
          case xop_floor:
          case xop_ceil:
          case xop_trunc:
          case xop_iround:
          case xop_ifloor:
          case xop_iceil:
          case xop_itrunc:
          case xop_lzcnt:
          case xop_tzcnt:
          case xop_popcnt:
            // unary
            queue.append(
              +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
              {
                const auto& up = head->uparam;
                auto& top = ctx.stack().mut_top();
                auto& rhs = up.b0 ? top.dereference_mutable() : top.dereference_copy();

                switch(up.u1) {
                  case xop_pos: {
                    // This operator does nothing.
                    return air_status_next;
                  }

                  case xop_neg: {
                    // Get the additive inverse of the operand.
                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      int64_t result;
                      if(ROCKET_SUB_OVERFLOW(0, val, &result))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer negation overflow (operand was `$1`)"),
                            val);

                      val = result;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      V_real& val = rhs.mut_real();

                      int64_t bits;
                      ::memcpy(&bits, &val, sizeof(val));
                      bits ^= INT64_MIN;

                      ::memcpy(&val, &bits, sizeof(val));
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Arithmetic negation not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_notb: {
                    // Flip all bits (of all bytes) in the operand.
                    if(rhs.type() == type_boolean) {
                      V_boolean& val = rhs.mut_boolean();
                      val = !val;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();
                      val = ~val;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_string) {
                      V_string& val = rhs.mut_string();
                      for(auto it = val.mut_begin();  it != val.end();  ++it)
                        *it = static_cast<char>(*it ^ -1);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Bitwise NOT not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_notl: {
                    // Perform the builtin boolean conversion and negate the result.
                    rhs = !rhs.test();
                    return air_status_next;
                  }

                  case xop_countof: {
                    // Get the number of elements in the operand.
                    if(rhs.type() == type_null) {
                      rhs = V_integer(0);
                      return air_status_next;
                    }
                    else if(rhs.type() == type_string) {
                      rhs = V_integer(rhs.as_string().size());
                      return air_status_next;
                    }
                    else if(rhs.type() == type_array) {
                      rhs = V_integer(rhs.as_array().size());
                      return air_status_next;
                    }
                    else if(rhs.type() == type_object) {
                      rhs = V_integer(rhs.as_object().size());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`countof` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_typeof: {
                    // Ge the type of the operand as a string.
                    rhs = ::rocket::sref(describe_type(rhs.type()));
                    return air_status_next;
                  }

                  case xop_sqrt: {
                    // Get the arithmetic square root of the operand, as a real number.
                    if(rhs.is_real()) {
                      rhs = ::std::sqrt(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__sqrt` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_isnan: {
                    // Checks whether the operand is a NaN. The operand must be of an
                    // arithmetic type. An integer is never a NaN.
                    if(rhs.type() == type_integer) {
                      rhs = false;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = ::std::isnan(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__isnan` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_isinf: {
                    // Checks whether the operand is an infinity. The operand must be of
                    // an arithmetic type. An integer is never an infinity.
                    if(rhs.type() == type_integer) {
                      rhs = false;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = ::std::isinf(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__isinf` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_abs: {
                    // Get the absolute value of the operand.
                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      V_integer neg_val;
                      if(ROCKET_SUB_OVERFLOW(0, val, &neg_val))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer negation overflow (operand was `$1`)"),
                            val);

                      val ^= (val ^ neg_val) & (val >> 63);
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      V_real& val = rhs.mut_real();

                      double result = ::std::fabs(val);

                      val = result;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__abs` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_sign: {
                    // Get the sign bit of the operand as a boolean value.
                    if(rhs.type() == type_integer) {
                      rhs = rhs.as_integer() < 0;
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = ::std::signbit(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__sign` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_round: {
                    // Round the operand to the nearest integer of the same type.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs.mut_real() = ::std::round(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__round` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_floor: {
                    // Round the operand to the nearest integer of the same type,
                    // towards negative infinity.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs.mut_real() = ::std::floor(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__floor` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_ceil: {
                    // Round the operand to the nearest integer of the same type,
                    // towards positive infinity.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs.mut_real() = ::std::ceil(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__ceil` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_trunc: {
                    // Truncate the operand to the nearest integer towards zero.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs.mut_real() = ::std::trunc(rhs.as_real());
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__trunc` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_iround: {
                    // Round the operand to the nearest integer.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = safe_double_to_int64(::std::round(rhs.as_real()));
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__iround` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_ifloor: {
                    // Round the operand to the nearest integer towards negative infinity.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = safe_double_to_int64(::std::floor(rhs.as_real()));
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__ifloor` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_iceil: {
                    // Round the operand to the nearest integer towards positive infinity.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = safe_double_to_int64(::std::ceil(rhs.as_real()));
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__iceil` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_itrunc: {
                    // Truncate the operand to the nearest integer towards zero.
                    if(rhs.type() == type_integer) {
                      return air_status_next;
                    }
                    else if(rhs.type() == type_real) {
                      rhs = safe_double_to_int64(::std::trunc(rhs.as_real()));
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__itrunc` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_lzcnt: {
                    // Get the number of leading zeroes in the operand.
                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      val = (int64_t) ROCKET_LZCNT64((uint64_t) val);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__lzcnt` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_tzcnt: {
                    // Get the number of trailing zeroes in the operand.
                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      val = (int64_t) ROCKET_TZCNT64((uint64_t) val);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__tzcnt` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  case xop_popcnt: {
                    // Get the number of ones in the operand.
                    if(rhs.type() == type_integer) {
                      V_integer& val = rhs.mut_integer();

                      val = (int64_t) ROCKET_POPCNT64((uint64_t) val);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "`__popcnt` not applicable (operand was `$1`)"),
                          rhs);
                  }

                  default:
                    ROCKET_UNREACHABLE();
                }
              }

              // Uparam
              , up2

              // Sparam
              // (none)

              // Collector
              // (none)

              // Symbols
              , &(altr.sloc)
            );
            return;

          case xop_cmp_eq:
          case xop_cmp_ne:
          case xop_cmp_lt:
          case xop_cmp_gt:
          case xop_cmp_lte:
          case xop_cmp_gte:
          case xop_cmp_3way:
          case xop_cmp_un:
          case xop_add:
          case xop_sub:
          case xop_mul:
          case xop_div:
          case xop_mod:
          case xop_andb:
          case xop_orb:
          case xop_xorb:
          case xop_addm:
          case xop_subm:
          case xop_mulm:
          case xop_adds:
          case xop_subs:
          case xop_muls:
            // binary
            queue.append(
              +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
              {
                const auto& up = head->uparam;
                const auto& rhs = ctx.stack().top().dereference_readonly();
                ctx.stack().pop();
                auto& top = ctx.stack().mut_top();
                auto& lhs = up.b0 ? top.dereference_mutable() : top.dereference_copy();

                switch(up.u1) {
                  case xop_cmp_eq: {
                    // Check whether the two operands are equal. Unordered values are
                    // considered to be unequal.
                    lhs = lhs.compare_partial(rhs) == compare_equal;
                    return air_status_next;
                  }

                  case xop_cmp_ne: {
                    // Check whether the two operands are not equal. Unordered values are
                    // considered to be unequal.
                    lhs = lhs.compare_partial(rhs) != compare_equal;
                    return air_status_next;
                  }

                  case xop_cmp_lt: {
                    // Check whether the LHS operand is less than the RHS operand. If
                    // they are unordered, an exception shall be thrown.
                    lhs = lhs.compare_total(rhs) == compare_less;
                    return air_status_next;
                  }

                  case xop_cmp_gt: {
                    // Check whether the LHS operand is greater than the RHS operand. If
                    // they are unordered, an exception shall be thrown.
                    lhs = lhs.compare_total(rhs) == compare_greater;
                    return air_status_next;
                  }

                  case xop_cmp_lte: {
                    // Check whether the LHS operand is less than or equal to the RHS
                    // operand. If they are unordered, an exception shall be thrown.
                    lhs = lhs.compare_total(rhs) != compare_greater;
                    return air_status_next;
                  }

                  case xop_cmp_gte: {
                    // Check whether the LHS operand is greater than or equal to the RHS
                    // operand. If they are unordered, an exception shall be thrown.
                    lhs = lhs.compare_total(rhs) != compare_less;
                    return air_status_next;
                  }

                  case xop_cmp_3way: {
                    // Perform 3-way comparison.
                    auto cmp = lhs.compare_partial(rhs);
                    if(ROCKET_UNEXPECT(cmp == compare_unordered))
                      lhs = sref("[unordered]");
                    else
                      lhs = -1LL + (cmp != compare_less) + (cmp == compare_greater);
                    return air_status_next;
                  }

                  case xop_cmp_un: {
                    // Check whether the two operands are unordered.
                    lhs = lhs.compare_partial(rhs) == compare_unordered;
                    return air_status_next;
                  }

                  case xop_add: {
                    // Perform logical OR on two boolean values, or get the sum of two
                    // arithmetic values, or concatenate two strings.
                    if(lhs.is_boolean() && rhs.is_boolean()) {
                      V_boolean& val = lhs.mut_boolean();
                      V_boolean other = rhs.as_boolean();

                      val |= other;
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      int64_t result;
                      if(ROCKET_ADD_OVERFLOW(val, other, &result))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer addition overflow (operands were `$1` and `$2`)"),
                            val, other);

                      val = result;
                      return air_status_next;
                    }
                    else if(lhs.is_real() && rhs.is_real()) {
                      V_real& val = lhs.mut_real();
                      V_real other = rhs.as_real();

                      val += other;
                      return air_status_next;
                    }
                    else if(lhs.is_string() && rhs.is_string()) {
                      V_string& val = lhs.mut_string();
                      const V_string& other = rhs.as_string();

                      val.append(other);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Addition not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_sub: {
                    // Perform logical XOR on two boolean values, or get the difference
                    // of two arithmetic values.
                    if(lhs.is_boolean() && rhs.is_boolean()) {
                      V_boolean& val = lhs.mut_boolean();
                      V_boolean other = rhs.as_boolean();

                      // Perform logical XOR of the operands.
                      val ^= other;
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      // Perform arithmetic subtraction with overflow checking.
                      int64_t result;
                      if(ROCKET_SUB_OVERFLOW(val, other, &result))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer subtraction overflow (operands were `$1` and `$2`)"),
                            val, other);

                      val = result;
                      return air_status_next;
                    }
                    else if(lhs.is_real() && rhs.is_real()) {
                      V_real& val = lhs.mut_real();
                      V_real other = rhs.as_real();

                      // Overflow will result in an infinity, so this is safe.
                      val -= other;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Subtraction not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_mul: {
                     // Perform logical AND on two boolean values, or get the product of
                     // two arithmetic values, or duplicate a string or array by a given
                     // times.
                    if(lhs.is_boolean() && rhs.is_boolean()) {
                      V_boolean& val = lhs.mut_boolean();
                      V_boolean other = rhs.as_boolean();

                      val &= other;
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      int64_t result;
                      if(ROCKET_MUL_OVERFLOW(val, other, &result))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer multiplication overflow (operands were `$1` and `$2`)"),
                            val, other);

                      val = result;
                      return air_status_next;
                    }
                    else if(lhs.is_real() && rhs.is_real()) {
                      V_real& val = lhs.mut_real();
                      V_real other = rhs.as_real();

                      val *= other;
                      return air_status_next;
                    }
                    else if(lhs.is_string() && rhs.is_integer()) {
                      V_string& val = lhs.mut_string();
                      V_integer count = rhs.as_integer();

                      do_duplicate_sequence_common(val, count);
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_string()) {
                      V_integer count = lhs.as_integer();
                      lhs = rhs.as_string();
                      V_string& val = lhs.mut_string();

                      do_duplicate_sequence_common(val, count);
                      return air_status_next;
                    }
                    else if(lhs.is_array() && rhs.is_integer()) {
                      V_array& val = lhs.mut_array();
                      V_integer count = rhs.as_integer();

                      do_duplicate_sequence_common(val, count);
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_array()) {
                      V_integer count = lhs.as_integer();
                      lhs = rhs.as_array();
                      V_array& val = lhs.mut_array();

                      do_duplicate_sequence_common(val, count);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Multiplication not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_div: {
                    // Get the quotient of two arithmetic values. If both operands are
                    // integers, the result is also an integer, truncated towards zero.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      if(other == 0)
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Zero as divisor (operands were `$1` and `$2`)"),
                            val, other);

                      if((val == INT64_MIN) && (other == -1))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer division overflow (operands were `$1` and `$2`)"),
                            val, other);

                      val /= other;
                      return air_status_next;
                    }
                    else if(lhs.is_real() && rhs.is_real()) {
                      V_real& val = lhs.mut_real();
                      V_real other = rhs.as_real();

                      val /= other;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Division not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_mod: {
                    // Get the remainder of two arithmetic values. The quotient is
                    // truncated towards zero. If both operands are integers, the result
                    // is also an integer.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      if(other == 0)
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Zero as divisor (operands were `$1` and `$2`)"),
                            val, other);

                      if((val == INT64_MIN) && (other == -1))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Integer division overflow (operands were `$1` and `$2`)"),
                            val, other);

                      val %= other;
                      return air_status_next;
                    }
                    else if(lhs.is_real() && rhs.is_real()) {
                      V_real& val = lhs.mut_real();
                      V_real other = rhs.as_real();

                      val = ::std::fmod(val, other);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Modulo not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_andb: {
                    // Perform the bitwise AND operation on all bits of the operands. If
                    // the two operands have different lengths, the result is truncated
                    // to the same length as the shorter one.
                    if(lhs.is_boolean() && rhs.is_boolean()) {
                      V_boolean& val = lhs.mut_boolean();
                      V_boolean other = rhs.as_boolean();

                      val &= other;
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      val &= other;
                      return air_status_next;
                    }
                    else if(lhs.is_string() && rhs.is_string()) {
                      V_string& val = lhs.mut_string();
                      const V_string& mask = rhs.as_string();

                      if(val.size() > mask.size())
                        val.erase(mask.size());
                      auto maskp = mask.begin();
                      for(auto it = val.mut_begin();  it != val.end();  ++it, ++maskp)
                        *it = static_cast<char>(*it & *maskp);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Bitwise AND not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_orb: {
                    // Perform the bitwise OR operation on all bits of the operands. If
                    // the two operands have different lengths, the result is padded to
                    // the same length as the longer one, with zeroes.
                    if(lhs.is_boolean() && rhs.is_boolean()) {
                      V_boolean& val = lhs.mut_boolean();
                      V_boolean other = rhs.as_boolean();

                      val |= other;
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      val |= other;
                      return air_status_next;
                    }
                    else if(lhs.is_string() && rhs.is_string()) {
                      V_string& val = lhs.mut_string();
                      const V_string& mask = rhs.as_string();

                      if(val.size() < mask.size())
                        val.append(mask.size() - val.size(), 0);
                      auto valp = val.mut_begin();
                      for(auto it = mask.begin();  it != mask.end();  ++it, ++valp)
                        *valp = static_cast<char>(*valp | *it);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Bitwise OR not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_xorb: {
                    // Perform the bitwise XOR operation on all bits of the operands. If
                    // the two operands have different lengths, the result is padded to
                    // the same length as the longer one, with zeroes.
                    if(lhs.is_boolean() && rhs.is_boolean()) {
                      V_boolean& val = lhs.mut_boolean();
                      V_boolean other = rhs.as_boolean();

                      val ^= other;
                      return air_status_next;
                    }
                    else if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      val ^= other;
                      return air_status_next;
                    }
                    else if(lhs.is_string() && rhs.is_string()) {
                      V_string& val = lhs.mut_string();
                      const V_string& mask = rhs.as_string();

                      if(val.size() < mask.size())
                        val.append(mask.size() - val.size(), 0);
                      auto valp = val.mut_begin();
                      for(auto it = mask.begin();  it != mask.end();  ++it, ++valp)
                        *valp = static_cast<char>(*valp ^ *it);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Bitwise XOR not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_addm: {
                    // Perform modular addition on two integers.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      ROCKET_ADD_OVERFLOW(val, other, &val);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Modular addition not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_subm: {
                    // Perform modular subtraction on two integers.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      ROCKET_SUB_OVERFLOW(val, other, &val);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Modular subtraction not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_mulm: {
                    // Perform modular multiplication on two integers.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      ROCKET_MUL_OVERFLOW(val, other, &val);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Modular multiplication not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_adds: {
                    // Perform saturating addition on two integers.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      if(ROCKET_ADD_OVERFLOW(val, other, &val))
                        val = (other >> 63) ^ INT64_MAX;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Saturating addition not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_subs: {
                    // Perform saturating subtraction on two integers.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      if(ROCKET_SUB_OVERFLOW(val, other, &val))
                        val = (other >> 63) ^ INT64_MIN;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Saturating subtraction not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_muls: {
                    // Perform saturating multiplication on two integers.
                    if(lhs.is_integer() && rhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();
                      V_integer other = rhs.as_integer();

                      if(ROCKET_MUL_OVERFLOW(val, other, &val))
                        val = (val >> 63) ^ (other >> 63) ^ INT64_MAX;
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Saturating multiplication not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  default:
                    ROCKET_UNREACHABLE();
                }
              }

              // Uparam
              , up2

              // Sparam
              // (none)

              // Collector
              // (none)

              // Symbols
              , &(altr.sloc)
            );
            return;

          case xop_fma:
            // ternary
            queue.append(
              +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
              {
                const auto& up = head->uparam;
                const auto& rhs = ctx.stack().top().dereference_readonly();
                ctx.stack().pop();
                const auto& mid = ctx.stack().top().dereference_readonly();
                ctx.stack().pop();
                auto& top = ctx.stack().mut_top();
                auto& lhs = up.b0 ? top.dereference_mutable() : top.dereference_copy();

                switch(up.u1) {
                  case xop_fma: {
                    // Perform floating-point fused multiply-add.
                    if(lhs.is_real() && mid.is_real() && rhs.is_real()) {
                      V_real& val = lhs.mut_real();
                      V_real y_mul = mid.as_real();
                      V_real z_add = rhs.as_real();

                      val = ::std::fma(val, y_mul, z_add);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Fused multiply-add not applicable (operands were `$1`, `$2` and `$3`)"),
                          lhs, mid, rhs);
                  }

                  default:
                    ROCKET_UNREACHABLE();
                }
              }

              // Uparam
              , up2

              // Sparam
              // (none)

              // Collector
              // (none)

              // Symbols
              , &(altr.sloc)
            );
            return;

          case xop_sll:
          case xop_srl:
          case xop_sla:
          case xop_sra:
            // shift
            queue.append(
              +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
              {
                const auto& up = head->uparam;
                const auto& rhs = ctx.stack().top().dereference_readonly();
                ctx.stack().pop();
                auto& top = ctx.stack().mut_top();
                auto& lhs = up.b0 ? top.dereference_mutable() : top.dereference_copy();

                if(rhs.type() != type_integer)
                  ASTERIA_THROW_RUNTIME_ERROR((
                      "Invalid shift count (operands were `$1` and `$2`)"),
                      lhs, rhs);

                if(rhs.as_integer() < 0)
                  ASTERIA_THROW_RUNTIME_ERROR((
                      "Negative shift count (operands were `$1` and `$2`)"),
                      lhs, rhs);

                switch(up.u1) {
                  case xop_sll: {
                    // Shift the operand to the left. Elements that get shifted out are
                    // discarded. Vacuum elements are filled with default values. The
                    // width of the operand is unchanged.
                    if(lhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();

                      int64_t count = rhs.as_integer();
                      val = (int64_t) ((uint64_t) val << (count & 63));
                      val &= ((count - 64) >> 63);
                      return air_status_next;
                    }
                    else if(lhs.is_string()) {
                      V_string& val = lhs.mut_string();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.erase(0, tlen);
                      val.append(tlen, ' ');
                      return air_status_next;
                    }
                    else if(lhs.is_array()) {
                      V_array& val = lhs.mut_array();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.erase(0, tlen);
                      val.append(tlen);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Logical left shift not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_srl: {
                    // Shift the operand to the right. Elements that get shifted out are
                    // discarded. Vacuum elements are filled with default values. The
                    // width of the operand is unchanged.
                    if(lhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();

                      int64_t count = rhs.as_integer();
                      val = (int64_t) ((uint64_t) val >> (count & 63));
                      val &= ((count - 64) >> 63);
                      return air_status_next;
                    }
                    else if(lhs.is_string()) {
                      V_string& val = lhs.mut_string();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.pop_back(tlen);
                      val.insert(0, tlen, ' ');
                      return air_status_next;
                    }
                    else if(lhs.is_array()) {
                      V_array& val = lhs.mut_array();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.pop_back(tlen);
                      val.insert(0, tlen);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Logical right shift not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_sla: {
                    // Shift the operand to the left. No element is discarded from the
                    // left (for integers this means that bits which get shifted out
                    // shall all be the same with the sign bit). Vacuum elements are
                    // filled with default values.
                    if(lhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();

                      int64_t count = ::rocket::min(rhs.as_integer(), 63);
                      if((val != 0) && ((count != rhs.as_integer()) || (((val >> 63) ^ val) >> (63 - count) != 0)))
                        ASTERIA_THROW_RUNTIME_ERROR((
                            "Arithmetic left shift overflow (operands were `$1` and `$2`)"),
                            lhs, rhs);

                      val <<= count;
                      return air_status_next;
                    }
                    else if(lhs.is_string()) {
                      V_string& val = lhs.mut_string();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.append(tlen, ' ');
                      return air_status_next;
                    }
                    else if(lhs.is_array()) {
                      V_array& val = lhs.mut_array();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.append(tlen);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Arithmetic left shift not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  case xop_sra: {
                    // Shift the operand to the right. Elements that get shifted out are
                    // discarded. No element is filled in the left.
                    if(lhs.is_integer()) {
                      V_integer& val = lhs.mut_integer();

                      int64_t count = ::rocket::min(rhs.as_integer(), 63);
                      val >>= count;
                      return air_status_next;
                    }
                    else if(lhs.is_string()) {
                      V_string& val = lhs.mut_string();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.pop_back(tlen);
                      return air_status_next;
                    }
                    else if(lhs.is_array()) {
                      V_array& val = lhs.mut_array();

                      size_t tlen = ::rocket::min((size_t) rhs.as_integer(), val.size());
                      val.pop_back(tlen);
                      return air_status_next;
                    }
                    else
                      ASTERIA_THROW_RUNTIME_ERROR((
                          "Arithmetic right shift not applicable (operands were `$1` and `$2`)"),
                          lhs, rhs);
                  }

                  default:
                    ROCKET_UNREACHABLE();
                }
              }

              // Uparam
              , up2

              // Sparam
              // (none)

              // Collector
              // (none)

              // Symbols
              , &(altr.sloc)
            );
            return;

          default:
            ASTERIA_TERMINATE(("Invalid operator enumerator `$1`"), altr.xop);
        }
      }

      case index_unpack_struct_array: {
        const auto& altr = this->m_stor.as<S_unpack_struct_array>();

        Uparam up2;
        up2.b0 = altr.immutable;
        up2.u2345 = altr.nelems;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            // Read the value of the initializer.
            const auto& init = ctx.stack().top().dereference_readonly();
            ctx.stack().pop();

            // Make sure it is really an array.
            if(!init.is_null() && !init.is_array())
              ASTERIA_THROW_RUNTIME_ERROR((
                  "Initializer was not an array (value was `$1`)"),
                  init);

            for(uint32_t i = up.u2345 - 1;  i != UINT32_MAX;  --i) {
              // Pop variables from from right to left.
              auto var = ctx.stack().top().unphase_variable_opt();
              ctx.stack().pop();
              ROCKET_ASSERT(var && !var->is_initialized());

              if(ROCKET_EXPECT(init.is_array()))
                if(auto ielem = init.as_array().ptr(i))
                  var->initialize(*ielem);

              if(ROCKET_UNEXPECT(!var->is_initialized()))
                var->initialize(nullopt);

              var->set_immutable(up.b0);
            }
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_unpack_struct_object: {
        const auto& altr = this->m_stor.as<S_unpack_struct_object>();

        Uparam up2;
        up2.b0 = altr.immutable;

        struct Sparam
          {
            cow_vector<phsh_string> keys;
          };

        Sparam sp2;
        sp2.keys = altr.keys;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Read the value of the initializer.
            const auto& init = ctx.stack().top().dereference_readonly();
            ctx.stack().pop();

            // Make sure it is really an object.
            if(!init.is_null() && !init.is_object())
              ASTERIA_THROW_RUNTIME_ERROR((
                  "Initializer was not an object (value was `$1`)"),
                  init);

            for(auto it = sp.keys.rbegin();  it != sp.keys.rend();  ++it) {
              // Pop variables from from right to left.
              auto var = ctx.stack().top().unphase_variable_opt();
              ctx.stack().pop();
              ROCKET_ASSERT(var && !var->is_initialized());

              if(ROCKET_EXPECT(init.is_object()))
                if(auto ielem = init.as_object().ptr(*it))
                  var->initialize(*ielem);

              if(ROCKET_UNEXPECT(!var->is_initialized()))
                var->initialize(nullopt);

              var->set_immutable(up.b0);
            }
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_define_null_variable: {
        const auto& altr = this->m_stor.as<S_define_null_variable>();

        Uparam up2;
        up2.b0 = altr.immutable;

        struct Sparam
          {
            phsh_string name;
          };

        Sparam sp2;
        sp2.name = altr.name;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            const auto& sloc = head->pv_meta->sloc;

            // Allocate a variable and inject it into the current context.
            const auto gcoll = ctx.global().garbage_collector();
            const auto var = gcoll->create_variable();
            ctx.insert_named_reference(sp.name).set_variable(var);
            ASTERIA_CALL_GLOBAL_HOOK(ctx.global(), on_variable_declare, sloc, sp.name);

            // Initialize it to null.
            var->initialize(nullopt);
            var->set_immutable(up.b0);
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_single_step_trap: {
        const auto& altr = this->m_stor.as<S_single_step_trap>();

        (void) altr;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            ASTERIA_CALL_GLOBAL_HOOK(ctx.global(), on_single_step_trap, head->pv_meta->sloc);
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_variadic_call: {
        const auto& altr = this->m_stor.as<S_variadic_call>();

        Uparam up2;
        up2.u0 = altr.ptc;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sloc = head->pv_meta->sloc;

            // Get the argument generator.
            const auto sentry = ctx.global().copy_recursion_sentry();
            ASTERIA_CALL_GLOBAL_HOOK(ctx.global(), on_single_step_trap, sloc);

            auto& alt_stack = ctx.alt_stack();
            auto& stack = ctx.stack();
            auto val = stack.top().dereference_readonly();

            if(val.is_null()) {
              // There is no argument.
              alt_stack.clear();
              stack.pop();
            }
            else if(val.is_array()) {
              const auto& arr = val.as_array();

              // Push all arguments as temporaries from left to right.
              alt_stack.clear();
              stack.pop();

              for(size_t k = 0;  k != arr.size();  ++k)
                alt_stack.push().set_temporary(arr.at(k));
            }
            else if(val.is_function()) {
              const auto& gfunc = val.as_function();

              // Pass an empty argument stack to get the number of arguments to
              // generate. This destroys the `this` reference so we have to stash
              // it first.
              auto gself = stack.mut_top().pop_modifier();
              alt_stack.clear();
              do_invoke_nontail(stack.mut_top(), ctx.global(), sloc, gfunc, ::std::move(alt_stack));
              const auto gnargs = stack.top().dereference_readonly();
              stack.pop();

              if(!gnargs.is_integer())
                ASTERIA_THROW_RUNTIME_ERROR((
                    "Variadic argument count was not valid (value `$1`)"),
                    gnargs);

              if(gnargs.as_integer() < 0)
                ASTERIA_THROW_RUNTIME_ERROR((
                    "Variadic argument count was negative (value `$1`)"),
                    gnargs);

              for(V_integer k = 0;  k != gnargs.as_integer();  ++k) {
                // Call the argument generator with the variadic argument index as
                // its sole argument. The `this` reference is copied from the very
                // first call.
                stack.push() = gself;
                alt_stack.clear();
                alt_stack.push().set_temporary(k);
                do_invoke_nontail(stack.mut_top(), ctx.global(), sloc, gfunc, ::std::move(alt_stack));
                stack.top().dereference_readonly();
              }

              do_pop_positional_arguments(alt_stack, stack, static_cast<uint32_t>(gnargs.as_integer()));
            }
            else
              ASTERIA_THROW_RUNTIME_ERROR(("Invalid argument generator (value `$1`)"), val);

            // Copy the target, which shall be of type `function`.
            val = stack.top().dereference_readonly();
            if(val.is_null())
              ASTERIA_THROW_RUNTIME_ERROR(("Function not found"));
            else if(!val.is_function())
              ASTERIA_THROW_RUNTIME_ERROR(("Attempt to call a non-function (value `$1`)"), val);

            const auto& target = val.as_function();
            auto& self = stack.mut_top().pop_modifier();
            stack.clear_cache();
            alt_stack.clear_cache();

            return ROCKET_EXPECT(up.u0 == ptc_aware_none)
                     ? do_invoke_nontail(self, ctx.global(), sloc, target, ::std::move(alt_stack))
                     : do_invoke_tail(self, static_cast<PTC_Aware>(up.u0), sloc, target, ::std::move(alt_stack));
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_defer_expression: {
        const auto& altr = this->m_stor.as<S_defer_expression>();

        struct Sparam
          {
            cow_vector<AIR_Node> code_body;
          };

        Sparam sp2;
        sp2.code_body = altr.code_body;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            const auto& sloc = head->pv_meta->sloc;

            // Capture local references at this time.
            bool dirty = false;
            auto bound_body = sp.code_body;
            do_rebind_nodes(dirty, bound_body, ctx);

            // Instantiate the expression and push it to the current context.
            AVMC_Queue queue_body;
            do_solidify_nodes(queue_body, bound_body);
            ctx.defer_expression(sloc, ::std::move(queue_body));
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            do_collect_variables_for_each(staged, temp, sp.code_body);
          }

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_import_call: {
        const auto& altr = this->m_stor.as<S_import_call>();

        Uparam up2;
        up2.u2345 = altr.nargs;

        struct Sparam
          {
            Compiler_Options opts;
          };

        Sparam sp2;
        sp2.opts = altr.opts;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            const auto& sloc = head->pv_meta->sloc;

            // Pop arguments off the stack from right to left.
            const auto sentry = ctx.global().copy_recursion_sentry();
            ASTERIA_CALL_GLOBAL_HOOK(ctx.global(), on_single_step_trap, sloc);

            auto& alt_stack = ctx.alt_stack();
            auto& stack = ctx.stack();
            ROCKET_ASSERT(up.u2345 != 0);
            do_pop_positional_arguments(alt_stack, stack, up.u2345 - 1);

            // Get the path of the file to import, which shall be a string.
            auto val = stack.top().dereference_readonly();
            if(!val.is_string())
              ASTERIA_THROW_RUNTIME_ERROR(("Path was not a string (value `$1`)"), val);

            auto path = ::std::move(val.mut_string());
            if(path.empty())
              ASTERIA_THROW_RUNTIME_ERROR(("Path was empty"));

            if(path[0] != '/') {
              // Convert this relative path to an absolute one.
              size_t slash = sloc.file().rfind('/');
              if(slash != cow_string::npos)
                path.insert(0, sloc.file(), 0, slash + 1);
              else
                path.insert(0, "/");
            }

            unique_ptr<char, void (void*)> abspath(::free);
            abspath.reset(::realpath(path.safe_c_str(), nullptr));
            if(!abspath)
              ASTERIA_THROW_RUNTIME_ERROR((
                  "Could not open script file '$1'",
                  "[`realpath()` failed: ${errno:full}]"),
                  path);

            // Parse the script file.
            path.assign(abspath);
            Module_Loader::Unique_Stream istrm;
            istrm.reset(ctx.global().module_loader(), path.c_str());

            Token_Stream tstrm(sp.opts);
            tstrm.reload(path, 1, ::std::move(istrm.get()));

            Statement_Sequence stmtq(sp.opts);
            stmtq.reload(::std::move(tstrm));

            // Instantiate the function.
            cow_vector<phsh_string> script_params;
            script_params.emplace_back(sref("..."));
            AIR_Optimizer optmz(sp.opts);
            optmz.reload(nullptr, script_params, ctx.global(), stmtq);

            Source_Location script_sloc(path, 0, 0);
            auto target = optmz.create_function(script_sloc, sref("[file scope]"));
            stack.clear_cache();
            alt_stack.clear_cache();

            // Invoke the script. `this` is `null`.
            auto& self = stack.mut_top();
            self.set_temporary(nullopt);
            do_invoke_nontail(self, ctx.global(), sloc, target, ::std::move(alt_stack));
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_declare_reference: {
        const auto& altr = this->m_stor.as<S_declare_reference>();

        struct Sparam
          {
            phsh_string name;
          };

        Sparam sp2;
        sp2.name = altr.name;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Declare a void reference.
            ctx.insert_named_reference(sp.name).clear();
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      case index_initialize_reference: {
        const auto& altr = this->m_stor.as<S_initialize_reference>();

        struct Sparam
          {
            phsh_string name;
          };

        Sparam sp2;
        sp2.name = altr.name;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Move a reference from the stack into the current context.
            ctx.insert_named_reference(sp.name) = ::std::move(ctx.stack().mut_top());
            ctx.stack().pop();
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_catch_expression: {
        const auto& altr = this->m_stor.as<S_catch_expression>();

        struct Sparam
          {
            AVMC_Queue queue_body;
          };

        Sparam sp2;
        do_solidify_nodes(sp2.queue_body, altr.code_body);

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);

            // Evaluate the expression in a `try` block. Its result is discarded.
            Value exval;
            const size_t old_stack_size = ctx.stack().size();
            AIR_Status status = air_status_next;
            try {
              status = sp.queue_body.execute(ctx);
              ROCKET_ASSERT(status == air_status_next);
            }
            catch(Runtime_Error& except) {
              exval = except.value();
            }

            // The stack shall be restored after the evaluation completes.
            while(ctx.stack().size() > old_stack_size)
              ctx.stack().pop();

            // Push a copy of the exception object.
            ROCKET_ASSERT(ctx.stack().size() == old_stack_size);
            ctx.stack().push().set_temporary(::std::move(exval));
            return air_status_next;
          }

          // Uparam
          // (none)

          // Sparam
          , sizeof(sp2), do_avmc_ctor<Sparam>, &sp2, do_avmc_dtor<Sparam>

          // Collector
          , +[](Variable_HashMap& staged, Variable_HashMap& temp, const Header* head)
          {
            const auto& sp = *reinterpret_cast<const Sparam*>(head->sparam);
            sp.queue_body.collect_variables(staged, temp);
          }

          // Symbols
          // (none)
        );
        return;
      }

      case index_return_statement: {
        const auto& altr = this->m_stor.as<S_return_statement>();

        Uparam up2;
        up2.b0 = altr.by_ref;
        up2.b1 = altr.is_void;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            if(up.b1 || ctx.stack().top().is_void()) {
              // Discard the result.
              return air_status_return_void;
            }
            else if(up.b0) {
              // The result is passed by reference, so check whether it is
              // dereferenceable.
              ctx.stack().top().dereference_readonly();
              return air_status_return_ref;
            }
            else {
              // The result is passed by copy, so convert it to a temporary.
              ctx.stack().mut_top().dereference_copy();
              return air_status_return_ref;
            }
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          , &(altr.sloc)
        );
        return;
      }

      case index_push_constant: {
        const auto& altr = this->m_stor.as<S_push_constant>();

        Uparam up2;
        up2.u0 = altr.airc;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            // Push a 'cheap' constant.
            switch(static_cast<AIR_Constant>(up.u0)) {
              case air_constant_null:
                // `null`
                ctx.stack().push().set_temporary(nullopt);
                return air_status_next;

              case air_constant_true:
                // `true`
                ctx.stack().push().set_temporary(true);
                return air_status_next;

              case air_constant_false:
                // `false`
                ctx.stack().push().set_temporary(false);
                return air_status_next;

              case air_constant_empty_str:
                // `""`
                ctx.stack().push().set_temporary(V_string());
                return air_status_next;

              case air_constant_empty_arr:
                // `[]`
                ctx.stack().push().set_temporary(V_array());
                return air_status_next;

              case air_constant_empty_obj:
                // `{}`
                ctx.stack().push().set_temporary(V_object());
                return air_status_next;

              default:
                 ASTERIA_TERMINATE(("Invalid AIR constant `$1`"), up.u0);
            }
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      case index_push_constant_int48: {
        const auto& altr = this->m_stor.as<S_push_constant_int48>();

        Uparam up2;
        up2.i01 = altr.high;
        up2.u2345 = altr.low;

        queue.append(
          +[](Executive_Context& ctx, const Header* head) ROCKET_FLATTEN -> AIR_Status
          {
            const auto& up = head->uparam;

            // Sign-extend the 48-bit integer and push it.
            ctx.stack().push().set_temporary(up.i01 * 0x100000000LL + up.u2345);
            return air_status_next;
          }

          // Uparam
          , up2

          // Sparam
          // (none)

          // Collector
          // (none)

          // Symbols
          // (none)
        );
        return;
      }

      default:
        ASTERIA_TERMINATE(("Invalid AIR node type (index `$1`)"), this->index());
    }
  }

}  // namespace asteria
