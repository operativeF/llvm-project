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

void test_derived_from_variant() {
  struct MyVariant : std::variant<short, long, float> {};

  std::visit<bool>(
      [](auto x) {
        assert(x == 42);
        return true;
      },
      MyVariant{42});
  std::visit<bool>(
      [](auto x) {
        assert(x == -1.3f);
        return true;
      },
      MyVariant{-1.3f});

  // Check that visit does not take index nor valueless_by_exception members from the base class.
  struct EvilVariantBase {
    int index;
    char valueless_by_exception;
  };

  struct EvilVariant1 : std::variant<int, long, double>,
                        std::tuple<int>,
                        EvilVariantBase {
    using std::variant<int, long, double>::variant;
  };

  std::visit<bool>(
      [](auto x) {
        assert(x == 12);
        return true;
      },
      EvilVariant1{12});
  std::visit<bool>(
      [](auto x) {
        assert(x == 12.3);
        return true;
      },
      EvilVariant1{12.3});

  // Check that visit unambiguously picks the variant, even if the other base has __impl member.
  struct ImplVariantBase {
    struct Callable {
      bool operator()() const { assert(false); return false; }
    };

    Callable __impl;
  };

  struct EvilVariant2 : std::variant<int, long, double>, ImplVariantBase {
    using std::variant<int, long, double>::variant;
  };

  std::visit<bool>(
      [](auto x) {
        assert(x == 12);
        return true;
      },
      EvilVariant2{12});
  std::visit<bool>(
      [](auto x) {
        assert(x == 12.3);
        return true;
      },
      EvilVariant2{12.3});
}

int main(int, char**) {
  test_derived_from_variant();

  return 0;
}
