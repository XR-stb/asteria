// This file is part of Asteria.
// Copyleft 2018 - 2021, LH_Mouse. All wrongs reserved.

#include "utils.hpp"
#include "../src/simple_script.hpp"

using namespace asteria;

int main()
  {
    Simple_Script code;
    code.reload_string(
      sref(__FILE__), __LINE__, sref(R"__(
///////////////////////////////////////////////////////////////////////////////

        var a = 1;
        func two() { return 2;  }
        func check() { return a &&= two();  }
        check();
        std.debug.logf("a = $1\n", a);

///////////////////////////////////////////////////////////////////////////////
      )__"));
    code.execute();
  }
