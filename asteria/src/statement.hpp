// This file is part of Asteria.
// Copyleft 2018, LH_Mouse. All wrongs reserved.

#ifndef ASTERIA_STATEMENT_HPP_
#define ASTERIA_STATEMENT_HPP_

#include "fwd.hpp"
#include "rocket/variant.hpp"

namespace Asteria {

class Statement {
public:
	enum Target_scope : unsigned {
		target_scope_unspecified  = 0,
		target_scope_switch       = 1,
		target_scope_while        = 2,
		target_scope_for          = 3,
	};

	enum Type : unsigned {
		type_expression_statement     =  0,
		type_variable_definition      =  1,
		type_function_definition      =  2,
		type_if_statement             =  3,
		type_switch_statement         =  4,
		type_do_while_statement       =  5,
		type_while_statement          =  6,
		type_for_statement            =  7,
		type_for_each_statement       =  8,
		type_try_statement            =  9,
		type_defer_statement          = 10,
		type_break_statement          = 11,
		type_continue_statement       = 12,
		type_throw_statement          = 13,
		type_return_statement         = 14,
	};
	struct S_expression_statement {
		Vp<Expression> expression_opt;
	};
	struct S_variable_definition {
		Cow_string identifier;
		bool constant;
		Vp<Initializer> initializer_opt;
	};
	struct S_function_definition {
		Cow_string identifier;
		Cow_string source_location;
		Sp_vector<const Parameter> parameters_opt;
		Vp<Block> body_opt;
	};
	struct S_if_statement {
		Vp<Expression> condition_opt;
		Vp<Block> branch_true_opt;
		Vp<Block> branch_false_opt;
	};
	struct S_switch_statement {
		Vp<Expression> control_opt;
		T_vector<T_pair<Vp<Expression>, Vp<Block>>> clauses_opt;
	};
	struct S_do_while_statement {
		Vp<Block> body_opt;
		Vp<Expression> condition_opt;
	};
	struct S_while_statement {
		Vp<Expression> condition_opt;
		Vp<Block> body_opt;
	};
	struct S_for_statement {
		Vp<Block> initialization_opt;
		Vp<Expression> condition_opt;
		Vp<Expression> increment_opt;
		Vp<Block> body_opt;
	};
	struct S_for_each_statement {
		Cow_string key_identifier;
		Cow_string value_identifier;
		Vp<Initializer> range_initializer_opt;
		Vp<Block> body_opt;
	};
	struct S_try_statement {
		Vp<Block> branch_try_opt;
		Cow_string exception_identifier;
		Vp<Block> branch_catch_opt;
	};
	struct S_defer_statement {
		Cow_string source_location;
		Vp<Block> body_opt;
	};
	struct S_break_statement {
		Target_scope target_scope;
	};
	struct S_continue_statement {
		Target_scope target_scope;
	};
	struct S_throw_statement {
		Vp<Expression> operand_opt;
	};
	struct S_return_statement {
		Vp<Expression> operand_opt;
	};
	using Variant = rocket::variant<ASTERIA_CDR(void
		, S_expression_statement    //  0
		, S_variable_definition     //  1
		, S_function_definition     //  2
		, S_if_statement            //  3
		, S_switch_statement        //  4
		, S_do_while_statement      //  5
		, S_while_statement         //  6
		, S_for_statement           //  7
		, S_for_each_statement      //  8
		, S_try_statement           //  9
		, S_defer_statement         // 10
		, S_break_statement         // 11
		, S_continue_statement      // 12
		, S_throw_statement         // 13
		, S_return_statement        // 14
	)>;

private:
	Variant m_variant;

public:
	template<typename CandidateT, ASTERIA_UNLESS_IS_BASE_OF(Statement, CandidateT)>
	Statement(CandidateT &&candidate)
		: m_variant(std::forward<CandidateT>(candidate))
	{ }
	Statement(Statement &&) noexcept;
	Statement & operator=(Statement &&) noexcept;
	~Statement();

public:
	Type get_type() const noexcept {
		return static_cast<Type>(m_variant.index());
	}
	template<typename ExpectT>
	const ExpectT * get_opt() const noexcept {
		return m_variant.try_get<ExpectT>();
	}
	template<typename ExpectT>
	const ExpectT & get() const {
		return m_variant.get<ExpectT>();
	}
};

}

#endif
