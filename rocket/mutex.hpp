// This file is part of Asteria.
// Copyleft 2018 - 2022, LH_Mouse. All wrongs reserved.

#ifndef ROCKET_MUTEX_
#define ROCKET_MUTEX_

#include "fwd.hpp"
#include "assert.hpp"
#include <mutex>
namespace rocket {

class mutex
  {
    friend class condition_variable;

  private:
    ::std::mutex m_mtx;

  public:
    mutex() = default;

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;

  public:
    // This is the only public interface, other than constructors
    // and the destructor.
    class unique_lock
      {
        friend class condition_variable;

      private:
        ::std::mutex* m_mtx = nullptr;

      public:
        constexpr
        unique_lock() noexcept = default;

        unique_lock(mutex& m) noexcept
          {
            m.m_mtx.lock();
            this->m_mtx = &(m.m_mtx);
          }

        unique_lock(unique_lock&& other) noexcept
          {
            this->m_mtx = other.m_mtx;
            other.m_mtx = nullptr;
          }

        unique_lock&
        operator=(unique_lock&& other) & noexcept
          {
            if(this->m_mtx == other.m_mtx)
              return *this;

            this->do_unlock_if();
            this->m_mtx = other.m_mtx;
            other.m_mtx = nullptr;
            return *this;
          }

        ~unique_lock()
          {
            this->do_unlock_if();
          }

      private:
        void
        do_unlock_if() const noexcept
          {
            if(this->m_mtx)
              this->m_mtx->unlock();
          }

      public:
        explicit constexpr operator
        bool() const noexcept
          { return this->m_mtx != nullptr;  }

        unique_lock&
        lock(mutex& m) noexcept
          {
            if(this->m_mtx == &(m.m_mtx))
              return *this;

            m.m_mtx.lock();
            this->do_unlock_if();
            this->m_mtx = &(m.m_mtx);
            return *this;
          }

        unique_lock&
        unlock() noexcept
          {
            ROCKET_ASSERT(this->m_mtx != nullptr);
            this->m_mtx->unlock();
            this->m_mtx = nullptr;
            return *this;
          }

        unique_lock&
        swap(unique_lock& other) noexcept
          {
            ::std::swap(this->m_mtx, other.m_mtx);
            return *this;
          }
      };
  };

inline
void
swap(mutex::unique_lock& lhs, mutex::unique_lock& rhs) noexcept
  {
    lhs.swap(rhs);
  }

}  // namespace rocket
#endif
