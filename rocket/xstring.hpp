// This file is part of Asteria.
// Copyleft 2018 - 2022, LH_Mouse. All wrongs reserved.

#ifndef ROCKET_XSTRING_
#define ROCKET_XSTRING_

#include "fwd.hpp"
namespace rocket {

#include "details/xstring.ipp"

template<typename charT>
constexpr
size_t
xstrlen(charT* str) noexcept
  {
    size_t ki = 0;
    for(;;)
      if(!ROCKET_CONSTANT_P(str[ki]))
        return details_xstring::xstrlen_nonconstexpr(str);
      else if(str[ki] == charT())
        return ki;
      else
        ki ++;
  }

template<typename charT>
constexpr
charT*
xstrchr(charT* str, typename identity<charT>::type target) noexcept
  {
    size_t ki = 0;
    for(;;)
      if(!ROCKET_CONSTANT_P(str[ki]) || !ROCKET_CONSTANT_P(target))
        return details_xstring::xstrchr_nonconstexpr(str, target);
      else if(str[ki] == target)
        return str + ki;
      else if(str[ki] == charT())
        return nullptr;
      else
        ki ++;
  }

template<typename charT>
constexpr
charT*
xmemchr(charT* str, typename identity<charT>::type target, size_t len) noexcept
  {
    size_t ki = 0;
    for(;;)
      if(ki >= len)
        return nullptr;
      else if(!ROCKET_CONSTANT_P(str[ki]) || !ROCKET_CONSTANT_P(target) || !ROCKET_CONSTANT_P(len))
        return details_xstring::xmemchr_nonconstexpr(str, target, len);
      else if(str[ki] == target)
        return str + ki;
      else
        ki ++;
  }

template<typename charT>
constexpr
int
xstrcmp(const charT* lhs, const charT* rhs) noexcept
  {
    using cmp_type = typename details_xstring::xcmp_type<charT>::type;
    size_t ki = 0;
    for(;;)
      if(!ROCKET_CONSTANT_P(lhs[ki]) || !ROCKET_CONSTANT_P(rhs[ki]))
        return details_xstring::xstrcmp_nonconstexpr(lhs, rhs);
      else if(lhs[ki] != rhs[ki])
        return ((cmp_type) lhs[ki] < (cmp_type) rhs[ki]) ? -1 : 1;
      else if(lhs[ki] == charT())
        return 0;
      else
        ki ++;
  }

template<typename charT>
constexpr
int
xmemcmp(const charT* lhs, const charT* rhs, size_t len) noexcept
  {
    using cmp_type = typename details_xstring::xcmp_type<charT>::type;
    size_t ki = 0;
    for(;;)
      if(ki >= len)
        return 0;
      else if(!ROCKET_CONSTANT_P(lhs[ki]) || !ROCKET_CONSTANT_P(rhs[ki]) || !ROCKET_CONSTANT_P(len))
        return details_xstring::xmemcmp_nonconstexpr(lhs, rhs, len);
      else if(lhs[ki] != rhs[ki])
        return ((cmp_type) lhs[ki] < (cmp_type) rhs[ki]) ? -1 : 1;
      else
        ki ++;
  }

template<typename charT>
constexpr
charT*
xmempset(charT* out, typename identity<charT>::type elem, size_t len) noexcept
  {
    size_t ki = 0;
    for(;;)
      if(ki >= len)
        return out + ki;
      else if(!ROCKET_CONSTANT_P(elem) || !ROCKET_CONSTANT_P(len))
        return details_xstring::xmempset_nonconstexpr(out, elem, len);
      else
        out[ki] = elem, ki ++;
  }

template<typename charT>
constexpr
charT*
xmemrpset(charT*& out, typename identity<charT>::type elem, size_t len) noexcept
  {
    return out = noadl::xmempset(out, elem, len);
  }

template<typename charT>
constexpr
charT*
xstrpcpy(charT* out, const typename identity<charT>::type* str) noexcept
  {
    size_t ki = 0;
    for(;;)
      if(!ROCKET_CONSTANT_P(str[ki]))
        return details_xstring::xstrpcpy_nonconstexpr(out, str);
      else if((out[ki] = str[ki]) == charT())
        return out + ki;
      else
        ki ++;
  }

template<typename charT>
constexpr
charT*
xstrrpcpy(charT*& out, const typename identity<charT>::type* str) noexcept
  {
    return out = noadl::xstrpcpy(out, str);
  }

template<typename charT>
constexpr
charT*
xmempcpy(charT* out, const typename identity<charT>::type* str, size_t len) noexcept
  {
    size_t ki = 0;
    for(;;)
      if(ki >= len)
        return out + ki;
      else if(!ROCKET_CONSTANT_P(str[ki]) || !ROCKET_CONSTANT_P(len))
        return details_xstring::xmempcpy_nonconstexpr(out, str, len);
      else
        out[ki] = str[ki], ki ++;
  }

template<typename charT>
constexpr
charT*
xmemrpcpy(charT*& out, const typename identity<charT>::type* str, size_t len) noexcept
  {
    return out = noadl::xmempcpy(out, str, len);
  }

}  // namespace rocket
#endif
