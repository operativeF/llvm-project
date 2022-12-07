//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// UNSUPPORTED: c++03, c++11, c++14, c++17

// Throwing bad_variant_access is supported starting in macosx10.13
// XFAIL: use_system_cxx_lib && target={{.+}}-apple-macosx10.{{9|10|11|12}} && !no-exceptions

// <variant>
// template <class R, class Visitor, class... Variants>
// constexpr R visit(Visitor&& vis, Variants&&... vars);

#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "test_macros.h"
#include "variant_test_helpers.h"

template <typename ReturnType>
void test_call_operator_forwarding() {
  using Fn = ForwardingCallObject;
  Fn obj{};
  const Fn &cobj = obj;
  { // test call operator forwarding - no variant
    std::visit<ReturnType>(obj);
    assert(Fn::check_call<>(CT_NonConst | CT_LValue));
    std::visit<ReturnType>(cobj);
    assert(Fn::check_call<>(CT_Const | CT_LValue));
    std::visit<ReturnType>(std::move(obj));
    assert(Fn::check_call<>(CT_NonConst | CT_RValue));
    std::visit<ReturnType>(std::move(cobj));
    assert(Fn::check_call<>(CT_Const | CT_RValue));
  }
  { // test call operator forwarding - single variant, single arg
    using V = std::variant<int>;
    V v(42);
    std::visit<ReturnType>(obj, v);
    assert(Fn::check_call<int &>(CT_NonConst | CT_LValue));
    std::visit<ReturnType>(cobj, v);
    assert(Fn::check_call<int &>(CT_Const | CT_LValue));
    std::visit<ReturnType>(std::move(obj), v);
    assert(Fn::check_call<int &>(CT_NonConst | CT_RValue));
    std::visit<ReturnType>(std::move(cobj), v);
    assert(Fn::check_call<int &>(CT_Const | CT_RValue));
  }
  { // test call operator forwarding - single variant, multi arg
    using V = std::variant<int, long, double>;
    V v(42l);
    std::visit<ReturnType>(obj, v);
    assert(Fn::check_call<long &>(CT_NonConst | CT_LValue));
    std::visit<ReturnType>(cobj, v);
    assert(Fn::check_call<long &>(CT_Const | CT_LValue));
    std::visit<ReturnType>(std::move(obj), v);
    assert(Fn::check_call<long &>(CT_NonConst | CT_RValue));
    std::visit<ReturnType>(std::move(cobj), v);
    assert(Fn::check_call<long &>(CT_Const | CT_RValue));
  }
  { // test call operator forwarding - multi variant, multi arg
    using V = std::variant<int, long, double>;
    using V2 = std::variant<int *, std::string>;
    V v(42l);
    V2 v2("hello");
    std::visit<int>(obj, v, v2);
    assert((Fn::check_call<long &, std::string &>(CT_NonConst | CT_LValue)));
    std::visit<ReturnType>(cobj, v, v2);
    assert((Fn::check_call<long &, std::string &>(CT_Const | CT_LValue)));
    std::visit<ReturnType>(std::move(obj), v, v2);
    assert((Fn::check_call<long &, std::string &>(CT_NonConst | CT_RValue)));
    std::visit<ReturnType>(std::move(cobj), v, v2);
    assert((Fn::check_call<long &, std::string &>(CT_Const | CT_RValue)));
  }
  {
    using V = std::variant<int, long, double, std::string>;
    V v1(42l), v2("hello"), v3(101), v4(1.1);
    std::visit<ReturnType>(obj, v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int &, double &>(CT_NonConst | CT_LValue)));
    std::visit<ReturnType>(cobj, v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int &, double &>(CT_Const | CT_LValue)));
    std::visit<ReturnType>(std::move(obj), v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int &, double &>(CT_NonConst | CT_RValue)));
    std::visit<ReturnType>(std::move(cobj), v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int &, double &>(CT_Const | CT_RValue)));
  }
  {
    using V = std::variant<int, long, double, int*, std::string>;
    V v1(42l), v2("hello"), v3(nullptr), v4(1.1);
    std::visit<ReturnType>(obj, v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int *&, double &>(CT_NonConst | CT_LValue)));
    std::visit<ReturnType>(cobj, v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int *&, double &>(CT_Const | CT_LValue)));
    std::visit<ReturnType>(std::move(obj), v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int *&, double &>(CT_NonConst | CT_RValue)));
    std::visit<ReturnType>(std::move(cobj), v1, v2, v3, v4);
    assert((Fn::check_call<long &, std::string &, int *&, double &>(CT_Const | CT_RValue)));
  }
}

int main(int, char**) {
    test_call_operator_forwarding<void>();
    test_call_operator_forwarding<int>();

    return 0;
}