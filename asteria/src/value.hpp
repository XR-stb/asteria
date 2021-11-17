// This file is part of Asteria.
// Copyleft 2018 - 2021, LH_Mouse. All wrongs reserved.

#ifndef ASTERIA_VALUE_HPP_
#define ASTERIA_VALUE_HPP_

#include "fwd.hpp"
#include "details/value.ipp"

namespace asteria {

class Value
  {
  private:
    ::rocket::variant<
      ROCKET_CDR(
        ,V_null      // 0,
        ,V_boolean   // 1,
        ,V_integer   // 2,
        ,V_real      // 3,
        ,V_string    // 4,
        ,V_opaque    // 5,
        ,V_function  // 6,
        ,V_array     // 7,
        ,V_object    // 8,
      )>
      m_stor;

  public:
    // Constructors and assignment operators
    constexpr
    Value(nullopt_t = nullopt) noexcept
      : m_stor()
      { }

    template<typename XValT,
    ROCKET_ENABLE_IF(details_value::Valuable<XValT>::direct_init::value)>
    Value(XValT&& xval)
      noexcept(::std::is_nothrow_constructible<decltype(m_stor),
                  typename details_value::Valuable<XValT>::via_type&&>::value)
      : m_stor(typename details_value::Valuable<XValT>::via_type(
                                        ::std::forward<XValT>(xval)))
      { }

    template<typename XValT,
    ROCKET_DISABLE_IF(details_value::Valuable<XValT>::direct_init::value)>
    Value(XValT&& xval)
      noexcept(::std::is_nothrow_assignable<decltype(m_stor)&,
                  typename details_value::Valuable<XValT>::via_type&&>::value)
      {
        details_value::Valuable<XValT>::assign(this->m_stor,
                                        ::std::forward<XValT>(xval));
      }

    template<typename XValT,
    ROCKET_ENABLE_IF_HAS_TYPE(typename details_value::Valuable<XValT>::via_type)>
    Value&
    operator=(XValT&& xval)
      noexcept(::std::is_nothrow_assignable<decltype(m_stor)&,
                  typename details_value::Valuable<XValT>::via_type&&>::value)
      {
        details_value::Valuable<XValT>::assign(this->m_stor,
                                           ::std::forward<XValT>(xval));
        return *this;
      }

  private:
    Variable_Callback&
    do_enumerate_variables_slow(Variable_Callback& callback) const;

    ROCKET_PURE bool
    do_test_slow() const noexcept;

    ROCKET_PURE static Compare
    do_compare_slow(const Value& lhs, const Value& rhs) noexcept;

  public:
    // Accessors
    Type
    type() const noexcept
      { return static_cast<Type>(this->m_stor.index());  }

    bool
    is_null() const noexcept
      { return this->type() == type_null;  }

    bool
    is_boolean() const noexcept
      { return this->type() == type_boolean;  }

    V_boolean
    as_boolean() const
      {
        if(this->type() == type_boolean)
          return this->m_stor.as<V_boolean>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting a `boolean`, but got `%s`)",
              describe_type(this->type()));
      }

    V_boolean&
    open_boolean()
      {
        if(this->type() == type_boolean)
          return this->m_stor.as<V_boolean>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting a `boolean`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_integer() const noexcept
      { return this->type() == type_integer;  }

    V_integer
    as_integer() const
      {
        if(this->type() == type_integer)
          return this->m_stor.as<V_integer>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `integer`, but got `%s`)",
              describe_type(this->type()));
      }

    V_integer&
    open_integer()
      {
        if(this->type() == type_integer)
          return this->m_stor.as<V_integer>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `integer`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_real() const noexcept
      {
        if(this->type() == type_real)
          return true;

        if(this->type() == type_integer)
          return true;

        return false;
      }

    V_real
    as_real() const
      {
        if(this->type() == type_real)
          return this->m_stor.as<V_real>();

        if(this->type() == type_integer)
          return static_cast<V_real>(this->m_stor.as<V_integer>());

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `integer` or `real`, but got `%s`)",
              describe_type(this->type()));
      }

    V_real&
    open_real()
      {
        if(this->type() == type_real)
          return this->m_stor.as<V_real>();

        if(this->type() == type_integer)
          return this->m_stor.emplace<V_real>(
                          static_cast<V_real>(this->m_stor.as<V_integer>()));

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `integer` or `real`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_string() const noexcept
      { return this->type() == type_string;  }

    const V_string&
    as_string() const
      {
        if(this->type() == type_string)
          return this->m_stor.as<V_string>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting a `string`, but got `%s`)",
              describe_type(this->type()));
      }

    V_string&
    open_string()
      {
        if(this->type() == type_string)
          return this->m_stor.as<V_string>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting a `string`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_function() const noexcept
      { return this->type() == type_function;  }

    const V_function&
    as_function() const
      {
        if(this->type() == type_function)
          return this->m_stor.as<V_function>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting a `function`, but got `%s`)",
              describe_type(this->type()));
      }

    V_function&
    open_function()
      {
        if(this->type() == type_function)
          return this->m_stor.as<V_function>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting a `string`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_opaque() const noexcept
      { return this->type() == type_opaque;  }

    const V_opaque&
    as_opaque() const
      {
        if(this->type() == type_opaque)
          return this->m_stor.as<V_opaque>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `opaque`, but got `%s`)",
              describe_type(this->type()));
      }

    V_opaque&
    open_opaque()
      {
        if(this->type() == type_opaque)
          return this->m_stor.as<V_opaque>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `opaque`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_array() const noexcept
      { return this->type() == type_array;  }

    const V_array&
    as_array() const
      {
        if(this->type() == type_array)
          return this->m_stor.as<V_array>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `array`, but got `%s`)",
              describe_type(this->type()));
      }

    V_array&
    open_array()
      {
        if(this->type() == type_array)
          return this->m_stor.as<V_array>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `array`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_object() const noexcept
      { return this->type() == type_object;  }

    const V_object&
    as_object() const
      {
        if(this->type() == type_object)
          return this->m_stor.as<V_object>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `object`, but got `%s`)",
              describe_type(this->type()));
      }

    V_object&
    open_object()
      {
        if(this->type() == type_object)
          return this->m_stor.as<V_object>();

        ::rocket::sprintf_and_throw<::std::invalid_argument>(
              "Value: type mismatch (expecting an `object`, but got `%s`)",
              describe_type(this->type()));
      }

    bool
    is_scalar() const noexcept
      {
        return (1 << this->type()) &
               (1 << type_null | 1 << type_boolean | 1 << type_integer |
                1 << type_real | 1 << type_string);
      }

    Value&
    swap(Value& other) noexcept
      {
        this->m_stor.swap(other.m_stor);
        return *this;
      }

    // This is used by garbage collection.
    Variable_Callback&
    enumerate_variables(Variable_Callback& callback) const
      {
        return this->is_scalar()
            ? callback
            : this->do_enumerate_variables_slow(callback);
      }

    // This performs the builtin conversion to boolean values.
    bool
    test() const noexcept
      {
        return this->is_null() ? false
            : this->is_boolean() ? this->as_boolean()
            : this->do_test_slow();
      }

    // This performs the builtin comparison with another value.
    Compare
    compare(const Value& other) const noexcept
      { return this->do_compare_slow(*this, other);  }

    // These are miscellaneous interfaces for debugging.
    tinyfmt&
    print(tinyfmt& fmt, bool escape = false) const;

    tinyfmt&
    dump(tinyfmt& fmt, size_t indent = 2, size_t hanging = 0) const;
  };

inline void
swap(Value& lhs, Value& rhs) noexcept
  { lhs.swap(rhs);  }

inline tinyfmt&
operator<<(tinyfmt& fmt, const Value& value)
  { return value.print(fmt);  }

}  // namespace asteria

#endif
