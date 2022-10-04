// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _PSTL_GLUE_EXECUTION_DEFS_H
#define _PSTL_GLUE_EXECUTION_DEFS_H

#include <type_traits>

#include "execution_defs.h"
#include "pstl_config.h"

namespace std
{
// Type trait
_PSTL_EXPORT_STD using __pstl::execution::is_execution_policy;
#if defined(_PSTL_CPP14_VARIABLE_TEMPLATES_PRESENT)
#    if defined(__INTEL_COMPILER)
_PSTL_EXPORT_STD template <class T>
constexpr bool is_execution_policy_v = is_execution_policy<T>::value;
#    else
_PSTL_EXPORT_STD using __pstl::execution::is_execution_policy_v;
#    endif
#endif

namespace execution
{
// Standard C++ policy classes
_PSTL_EXPORT_STD using __pstl::execution::parallel_policy;
_PSTL_EXPORT_STD using __pstl::execution::parallel_unsequenced_policy;
_PSTL_EXPORT_STD using __pstl::execution::sequenced_policy;

// Standard predefined policy instances
_PSTL_EXPORT_STD using __pstl::execution::par;
_PSTL_EXPORT_STD using __pstl::execution::par_unseq;
_PSTL_EXPORT_STD using __pstl::execution::seq;

// Implementation-defined names
// Unsequenced policy is not yet standard, but for consistency
// we include it into namespace std::execution as well
_PSTL_EXPORT_STD using __pstl::execution::unseq;
_PSTL_EXPORT_STD using __pstl::execution::unsequenced_policy;
} // namespace execution
} // namespace std

#include "algorithm_impl.h"
#include "numeric_impl.h"
#include "parallel_backend.h"

#endif /* _PSTL_GLUE_EXECUTION_DEFS_H */
