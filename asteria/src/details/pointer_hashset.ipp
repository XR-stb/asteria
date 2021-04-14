// This file is part of Asteria.
// Copyleft 2018 - 2021, LH_Mouse. All wrongs reserved.

#ifndef ASTERIA_LLDS_POINTER_HASHSET_HPP_
#  error Please include <asteria/llds/pointer_hashset.hpp> instead.
#endif

namespace asteria {
namespace details_pointer_hashset {

struct Bucket
  {
    Bucket* next;  // the next bucket in the [non-circular] list
    Bucket* prev;  // the previous bucket in the [circular] list
    const void* key_ptr;  // initialized iff `prev` is non-null

    void
    debug_clear() noexcept
      {
        ::std::memset(static_cast<void*>(this), 0xD3, sizeof(*this));
        this->prev = nullptr;
      }

    explicit operator
    bool() const noexcept
      { return this->prev != nullptr;  }
  };

}  // namespace details_pointer_hashset
}  // namespace asteria
