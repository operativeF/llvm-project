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

void test_return_type() {
  using Fn = ForwardingCallObject;
  Fn obj{};
  const Fn &cobj = obj;
  { // test call operator forwarding - no variant
    static_assert(std::is_same_v<decltype(std::visit(obj)), Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(cobj)), const Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(obj))), Fn&&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(cobj))), const Fn&&>);
  }
  { // test call operator forwarding - single variant, single arg
    using V = std::variant<int>;
    V v(42);
    static_assert(std::is_same_v<decltype(std::visit(obj, v)), Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(cobj, v)), const Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(obj), v)), Fn&&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(cobj), v)), const Fn&&>);
  }
  { // test call operator forwarding - single variant, multi arg
    using V = std::variant<int, long, double>;
    V v(42l);
    static_assert(std::is_same_v<decltype(std::visit(obj, v)), Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(cobj, v)), const Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(obj), v)), Fn&&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(cobj), v)), const Fn&&>);
  }
  { // test call operator forwarding - multi variant, multi arg
    using V = std::variant<int, long, double>;
    using V2 = std::variant<int *, std::string>;
    V v(42l);
    V2 v2("hello");
    static_assert(std::is_same_v<decltype(std::visit(obj, v, v2)), Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(cobj, v, v2)), const Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(obj), v, v2)), Fn&&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(cobj), v, v2)), const Fn&&>);
  }
  {
    using V = std::variant<int, long, double, std::string>;
    V v1(42l), v2("hello"), v3(101), v4(1.1);
    static_assert(std::is_same_v<decltype(std::visit(obj, v1, v2, v3, v4)), Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(cobj, v1, v2, v3, v4)), const Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(obj), v1, v2, v3, v4)), Fn&&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(cobj), v1, v2, v3, v4)), const Fn&&>);
  }
  {
    using V = std::variant<int, long, double, int*, std::string>;
    V v1(42l), v2("hello"), v3(nullptr), v4(1.1);
    static_assert(std::is_same_v<decltype(std::visit(obj, v1, v2, v3, v4)), Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(cobj, v1, v2, v3, v4)), const Fn&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(obj), v1, v2, v3, v4)), Fn&&>);
    static_assert(std::is_same_v<decltype(std::visit(std::move(cobj), v1, v2, v3, v4)), const Fn&&>);
  }
}

int main(int, char**) {
    test_return_type();

    return 0;
}