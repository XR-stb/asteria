// This file is part of Asteria.
// Copyleft 2018, LH_Mouse. All wrongs reserved.

#ifndef ASTERIA_RUNTIME_ABSTRACT_FUNCTION_HPP_
#define ASTERIA_RUNTIME_ABSTRACT_FUNCTION_HPP_

#include "../fwd.hpp"
#include "../rocket/refcounted_ptr.hpp"
#include "../rocket/cow_vector.hpp"

namespace Asteria {

class Abstract_function : public rocket::refcounted_base<Abstract_function>
  {
  public:
    Abstract_function() noexcept
      {
      }
    ROCKET_COPYABLE_DESTRUCTOR(Abstract_function, virtual);

  public:
    virtual rocket::cow_string describe() const = 0;
    virtual void invoke(Reference &self_io, Global_context &global, rocket::cow_vector<Reference> &&args) const = 0;
    virtual void enumerate_variables(const Abstract_variable_callback &callback) const = 0;
  };

}

#endif
