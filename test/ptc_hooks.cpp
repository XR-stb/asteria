// This file is part of Asteria.
// Copyleft 2018 - 2023, LH_Mouse. All wrongs reserved.

#include "utils.hpp"
#include "../asteria/simple_script.hpp"
#include "../asteria/source_location.hpp"
#include "../asteria/runtime/abstract_hooks.hpp"
#include "../asteria/runtime/runtime_error.hpp"
using namespace ::asteria;

int main()
  {
    struct Test_Hooks : Abstract_Hooks
      {
        ::rocket::tinyfmt_str fmt;

        virtual
        void
        on_function_call(const Source_Location& sloc, const cow_function& target)
          {
            (void)target;
            this->fmt << "call " << sloc.line() << "; ";
          }

        virtual
        void
        on_function_except(const Source_Location& sloc, const cow_function& target,
                           const Runtime_Error& except)
          {
            (void)target;
            (void)except;
            this->fmt << "except " << sloc.line() << "; ";
          }
      };

    const auto hooks = ::rocket::make_refcnt<Test_Hooks>();
    Simple_Script code;
    code.global().set_hooks(hooks);

    code.reload_string(
      sref(__FILE__), __LINE__, sref(R"__(
///////////////////////////////////////////////////////////////////////////////

        func no_ptc_throw() {
          throw "boom";
        }

        func no_ptc_three() {
          return no_ptc_throw() + 1;
        }

        func no_ptc_two() {
          return ref no_ptc_three() + 1;
        }

        func no_ptc_one() {
          return no_ptc_two() + 1;
        }

        no_ptc_one();

///////////////////////////////////////////////////////////////////////////////
      )__"));
    hooks->fmt.clear_string();
    ASTERIA_TEST_CHECK_CATCH(code.execute());
    ::fprintf(stderr, "no_ptc ===> %s\n", hooks->fmt.c_str());
    ASTERIA_TEST_CHECK(hooks->fmt.get_string() ==
        "call 60; call 57; call 53; call 49; except 49; except 53; except 57; except 60; ");

    code.reload_string(
      sref(__FILE__), __LINE__, sref(R"__(
///////////////////////////////////////////////////////////////////////////////

        func ptc_throw() {
          throw "boom";
        }

        func ptc_three() {
          return ptc_throw();
        }

        func ptc_two() {
          return ref ptc_three();
        }

        func ptc_one() {
          return ptc_two();
        }

        ptc_one();

///////////////////////////////////////////////////////////////////////////////
      )__"));
    hooks->fmt.clear_string();
    ASTERIA_TEST_CHECK_CATCH(code.execute());
    ::fprintf(stderr, "ptc ===> %s\n", hooks->fmt.c_str());
    ASTERIA_TEST_CHECK(hooks->fmt.get_string() ==
        "call 90; call 87; call 83; call 79; except 79; except 83; except 87; except 90; ");
  }
