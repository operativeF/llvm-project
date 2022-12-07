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

struct MyVariant : std::variant<short, long, float> {};

namespace std {
template <size_t Index>
void get(const MyVariant&) {
  assert(false);
}
} // namespace std

void test_derived_from_variant() {
  auto v1 = MyVariant{42};
  const auto cv1 = MyVariant{142};
  std::visit([](auto x) { assert(x == 42); }, v1);
  std::visit([](auto x) { assert(x == 142); }, cv1);
  std::visit([](auto x) { assert(x == -1.25f); }, MyVariant{-1.25f});
  std::visit([](auto x) { assert(x == 42); }, std::move(v1));
  std::visit([](auto x) { assert(x == 142); }, std::move(cv1));

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

  std::visit([](auto x) { assert(x == 12); }, EvilVariant1{12});
  std::visit([](auto x) { assert(x == 12.3); }, EvilVariant1{12.3});

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

  std::visit([](auto x) { assert(x == 12); }, EvilVariant2{12});
  std::visit([](auto x) { assert(x == 12.3); }, EvilVariant2{12.3});
}

int main(int, char**) {
    test_derived_from_variant();

    return 0;
}
