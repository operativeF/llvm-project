//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, c++11, c++14

// Throwing bad_variant_access is supported starting in macosx10.13
// XFAIL: use_system_cxx_lib && target={{.+}}-apple-macosx10.{{9|10|11|12}} && !no-exceptions

// <variant>
// template <class Visitor, class... Variants>
// constexpr see below visit(Visitor&& vis, Variants&&... vars);

#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "test_macros.h"
#include "variant_test_helpers.h"

void test_argument_forwarding() {
  using Fn = ForwardingCallObject;
  Fn obj{};
  const auto Val = CT_LValue | CT_NonConst;
  { // single argument - value type
    using V = std::variant<int>;
    V v(42);
    const V &cv = v;
    std::visit(obj, v);
    assert(Fn::check_call<int &>(Val));
    std::visit(obj, cv);
    assert(Fn::check_call<const int &>(Val));
    std::visit(obj, std::move(v));
    assert(Fn::check_call<int &&>(Val));
    std::visit(obj, std::move(cv));
    assert(Fn::check_call<const int &&>(Val));
  }
#if !defined(TEST_VARIANT_HAS_NO_REFERENCES)
  { // single argument - lvalue reference
    using V = std::variant<int &>;
    int x = 42;
    V v(x);
    const V &cv = v;
    std::visit(obj, v);
    assert(Fn::check_call<int &>(Val));
    std::visit(obj, cv);
    assert(Fn::check_call<int &>(Val));
    std::visit(obj, std::move(v));
    assert(Fn::check_call<int &>(Val));
    std::visit(obj, std::move(cv));
    assert(Fn::check_call<int &>(Val));
  }
  { // single argument - rvalue reference
    using V = std::variant<int &&>;
    int x = 42;
    V v(std::move(x));
    const V &cv = v;
    std::visit(obj, v);
    assert(Fn::check_call<int &>(Val));
    std::visit(obj, cv);
    assert(Fn::check_call<int &>(Val));
    std::visit(obj, std::move(v));
    assert(Fn::check_call<int &&>(Val));
    std::visit(obj, std::move(cv));
    assert(Fn::check_call<int &&>(Val));
  }
#endif
  { // multi argument - multi variant
    using V = std::variant<int, std::string, long>;
    V v1(42), v2("hello"), v3(43l);
    std::visit(obj, v1, v2, v3);
    assert((Fn::check_call<int &, std::string &, long &>(Val)));
    std::visit(obj, std::as_const(v1), std::as_const(v2), std::move(v3));
    assert((Fn::check_call<const int &, const std::string &, long &&>(Val)));
  }
  {
    using V = std::variant<int, long, double, std::string>;
    V v1(42l), v2("hello"), v3(101), v4(1.1);
    std::visit(obj, v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int &, double &>(Val)));
    std::visit(obj, std::as_const(v1), std::as_const(v2), std::move(v3), std::move(v4));
    assert((Fn::check_call<const long &, const std::string &, int &&, double &&>(Val)));
  }
  {
    using V = std::variant<int, long, double, int*, std::string>;
    V v1(42l), v2("hello"), v3(nullptr), v4(1.1);
    std::visit(obj, v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int *&, double &>(Val)));
    std::visit(obj, std::as_const(v1), std::as_const(v2), std::move(v3), std::move(v4));
    assert((Fn::check_call<const long &, const std::string &, int *&&, double &&>(Val)));
  }
}

int main(int, char**) {
    test_argument_forwarding();

    return 0;
}
