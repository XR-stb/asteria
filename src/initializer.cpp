// This file is part of asteria.
// Copyleft 2018, LH_Mouse. All wrongs reserved.

#include "precompiled.hpp"
#include "initializer.hpp"
#include "variable.hpp"
#include "stored_value.hpp"
#include "expression.hpp"
#include "reference.hpp"
#include "utilities.hpp"

namespace Asteria {

Initializer::Initializer(Initializer &&) noexcept = default;
Initializer &Initializer::operator=(Initializer &&) = default;
Initializer::~Initializer() = default;

Initializer::Type get_initializer_type(Spcref<const Initializer> initializer_opt) noexcept {
	return initializer_opt ? initializer_opt->get_type() : Initializer::type_null;
}

void initialize_variable(Xptr<Variable> &variable_out, Spcref<Recycler> recycler, Spcref<const Initializer> initializer_opt, Spcref<const Scope> scope){
	const auto type = get_initializer_type(initializer_opt);
	switch(type){
	case Initializer::type_null:
		return set_variable(variable_out, recycler, nullptr);
	case Initializer::type_assignment_init: {
		const auto &params = initializer_opt->get<Initializer::S_assignment_init>();
		Xptr<Reference> result;
		evaluate_expression(result, recycler, params.expression, scope);
		return extract_variable_from_reference(variable_out, recycler, std::move(result)); }
	case Initializer::type_bracketed_init_list: {
		const auto &params = initializer_opt->get<Initializer::S_bracketed_init_list>();
		D_array array;
		array.reserve(params.elements.size());
		for(const auto &elem : params.elements){
			initialize_variable(variable_out, recycler, elem, scope);
			array.emplace_back(std::move(variable_out));
		}
		return set_variable(variable_out, recycler, std::move(array)); }
	case Initializer::type_braced_init_list: {
		const auto &params = initializer_opt->get<Initializer::S_braced_init_list>();
		D_object object;
		object.reserve(params.key_values.size());
		for(const auto &pair : params.key_values){
			initialize_variable(variable_out, recycler, pair.second, scope);
			object.emplace(pair.first, std::move(variable_out));
		}
		return set_variable(variable_out, recycler, std::move(object)); }
	default:
		ASTERIA_DEBUG_LOG("Unknown initializer type enumeration: type = ", type);
		std::terminate();
	}
}

}
