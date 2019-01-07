/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef LIBTEXTCLASSIFIER_UTILS_VARIANT_H_
#define LIBTEXTCLASSIFIER_UTILS_VARIANT_H_

#include <string>

#include "utils/base/integral_types.h"
#include "utils/strings/stringpiece.h"

namespace libtextclassifier3 {

// Represents a type-tagged union of different basic types.
struct Variant {
  Variant() : type(TYPE_INVALID) {}
  explicit Variant(int value) : type(TYPE_INT_VALUE), int_value(value) {}
  explicit Variant(int64 value) : type(TYPE_LONG_VALUE), long_value(value) {}
  explicit Variant(float value) : type(TYPE_FLOAT_VALUE), float_value(value) {}
  explicit Variant(double value)
      : type(TYPE_DOUBLE_VALUE), double_value(value) {}
  explicit Variant(StringPiece value)
      : type(TYPE_STRING_VALUE), string_value(value.ToString()) {}
  explicit Variant(std::string value)
      : type(TYPE_STRING_VALUE), string_value(value) {}
  explicit Variant(const char* value)
      : type(TYPE_STRING_VALUE), string_value(value) {}
  explicit Variant(bool value) : type(TYPE_BOOL_VALUE), bool_value(value) {}
  enum Type {
    TYPE_INVALID = 0,
    TYPE_INT_VALUE = 1,
    TYPE_LONG_VALUE = 2,
    TYPE_FLOAT_VALUE = 3,
    TYPE_DOUBLE_VALUE = 4,
    TYPE_BOOL_VALUE = 5,
    TYPE_STRING_VALUE = 6,
  };
  Type type;
  union {
    int int_value;
    int64 long_value;
    float float_value;
    double double_value;
    bool bool_value;
  };
  std::string string_value;
};

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_UTILS_VARIANT_H_
