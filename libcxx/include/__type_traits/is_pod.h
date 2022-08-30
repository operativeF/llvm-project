//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_POD_H
#define _LIBCPP___TYPE_TRAITS_IS_POD_H

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS)
_LIBCPP_EXPORT_STD template <class _Tp> struct _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_TEMPLATE_VIS is_pod
    : public integral_constant<bool, __is_pod(_Tp)> {};

#if _LIBCPP_STD_VER > 14
_LIBCPP_EXPORT_STD template <class _Tp>
_LIBCPP_DEPRECATED_IN_CXX20 inline constexpr bool is_pod_v = __is_pod(_Tp);
#endif

#endif // _LIBCPP_STD_VER <= 17 || defined(_LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_POD_H
