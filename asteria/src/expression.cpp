// This file is part of Asteria.
// Copyleft 2018, LH_Mouse. All wrongs reserved.

#include "precompiled.hpp"
#include "expression.hpp"
#include "reference.hpp"
#include "utilities.hpp"

namespace Asteria {

Expression::Expression(Expression &&) noexcept = default;
Expression & Expression::operator=(Expression &&) noexcept = default;
Expression::~Expression() = default;

void bind_expression(Vp<Expression> &bound_expr_out, Sp_ref<const Expression> expression_opt, Sp_ref<const Scope> scope){
	if(expression_opt == nullptr){
		// Return a null expression.
		bound_expr_out.reset();
		return;
	}
	// Bind nodes recursively.
	Vector<Expression_node> bound_nodes;
	bound_nodes.reserve(expression_opt->size());
	for(const auto &node : *expression_opt){
		bind_expression_node(bound_nodes, node, scope);
	}
	bound_expr_out.emplace(std::move(bound_nodes));
}
void evaluate_expression(Vp<Reference> &result_out, Sp_ref<Recycler> recycler_out, Sp_ref<const Expression> expression_opt, Sp_ref<const Scope> scope){
	if(expression_opt == nullptr){
		// Return a null reference only when a null expression is given.
		move_reference(result_out, nullptr);
		return;
	}
	// Parameters are pushed from right to left, in lexical order.
	Vector<Vp<Reference>> stack;
	for(const auto &node : *expression_opt){
		evaluate_expression_node(stack, recycler_out, node, scope);
	}
	// Get the result. If the stack is empty or has more than one element, the expression is unbalanced.
	if(stack.size() != 1){
		ASTERIA_THROW_RUNTIME_ERROR("The expression was unbalanced. There should be exactly one reference left in the evaluation stack, but there were `", stack.size(), "`.");
	}
	move_reference(result_out, std::move(stack.front()));
}

}
