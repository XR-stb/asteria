// This file is part of Asteria.
// Copyleft 2018 - 2023, LH_Mouse. All wrongs reserved.

#include "utils.hpp"
#include "../asteria/simple_script.hpp"
using namespace ::asteria;

int main()
  {
    Simple_Script code;
    code.reload_string(
      &__FILE__, __LINE__, &R"__(
///////////////////////////////////////////////////////////////////////////////

        func three() {
          func two() {
            func one() {
              return typeof two;
            }
            return one();
          }
          return two();
        }
        return three();

///////////////////////////////////////////////////////////////////////////////
      )__");
    ASTERIA_TEST_CHECK(code.execute().dereference_readonly().as_string() == "function");
  }
