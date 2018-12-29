// This file is part of Asteria.
// Copyleft 2018, LH_Mouse. All wrongs reserved.

#ifndef ROCKET_COMPATIBILITY_H_
#define ROCKET_COMPATIBILITY_H_

#include <cstdbool>  // standard library configuration macros

#if defined(__clang__)
#  include "compatibility_clang.h"
#elif defined(__GNUC__)
#  include "compatibility_gcc.h"
#elif defined(_MSC_VER)
#  include "compatibility_msvc.h"
#else
#  error This compiler is not supported.
#endif

#endif
