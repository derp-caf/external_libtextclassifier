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

#include <map>
#include <string>

#include "utils/base/integral_types.h"
#include "utils/base/logging.h"
#include "utils/named-extra_generated.h"
#include "utils/strings/stringpiece.h"

namespace libtextclassifier3 {

// Represents a type-tagged union of different basic types.
class Variant {
 public:
  Variant() : type_(VariantValue_::Type_NONE) {}
  explicit Variant(int value)
      : type_(VariantValue_::Type_INT_VALUE), int_value_(value) {}
  explicit Variant(int64 value)
      : type_(VariantValue_::Type_INT64_VALUE), long_value_(value) {}
  explicit Variant(float value)
      : type_(VariantValue_::Type_FLOAT_VALUE), float_value_(value) {}
  explicit Variant(double value)
      : type_(VariantValue_::Type_DOUBLE_VALUE), double_value_(value) {}
  explicit Variant(StringPiece value)
      : type_(VariantValue_::Type_STRING_VALUE),
        string_value_(value.ToString()) {}
  explicit Variant(std::string value)
      : type_(VariantValue_::Type_STRING_VALUE), string_value_(value) {}
  explicit Variant(const char* value)
      : type_(VariantValue_::Type_STRING_VALUE), string_value_(value) {}
  explicit Variant(bool value)
      : type_(VariantValue_::Type_BOOL_VALUE), bool_value_(value) {}
  explicit Variant(const VariantValue* value) : type_(value->type()) {
    switch (type_) {
      case VariantValue_::Type_INT_VALUE:
        int_value_ = value->int_value();
        break;
      case VariantValue_::Type_INT64_VALUE:
        long_value_ = value->int64_value();
        break;
      case VariantValue_::Type_FLOAT_VALUE:
        float_value_ = value->float_value();
        break;
      case VariantValue_::Type_DOUBLE_VALUE:
        double_value_ = value->double_value();
        break;
      case VariantValue_::Type_BOOL_VALUE:
        bool_value_ = value->bool_value();
        break;
      case VariantValue_::Type_STRING_VALUE:
        string_value_ = value->string_value()->str();
        break;
      default:
        TC3_LOG(ERROR) << "Unknown variant type: " << type_;
    }
  }

  int IntValue() const {
    TC3_CHECK(HasInt());
    return int_value_;
  }

  int64 Int64Value() const {
    TC3_CHECK(HasInt64());
    return long_value_;
  }

  float FloatValue() const {
    TC3_CHECK(HasFloat());
    return float_value_;
  }

  double DoubleValue() const {
    TC3_CHECK(HasDouble());
    return double_value_;
  }

  bool BoolValue() const {
    TC3_CHECK(HasBool());
    return bool_value_;
  }

  const std::string& StringValue() const {
    TC3_CHECK(HasString());
    return string_value_;
  }

  bool HasInt() const { return type_ == VariantValue_::Type_INT_VALUE; }

  bool HasInt64() const { return type_ == VariantValue_::Type_INT64_VALUE; }

  bool HasFloat() const { return type_ == VariantValue_::Type_FLOAT_VALUE; }

  bool HasDouble() const { return type_ == VariantValue_::Type_DOUBLE_VALUE; }

  bool HasBool() const { return type_ == VariantValue_::Type_BOOL_VALUE; }

  bool HasString() const { return type_ == VariantValue_::Type_STRING_VALUE; }

  VariantValue_::Type GetType() const { return type_; }

  bool HasValue() const { return type_ != VariantValue_::Type_NONE; }

 private:
  VariantValue_::Type type_;
  union {
    int int_value_;
    int64 long_value_;
    float float_value_;
    double double_value_;
    bool bool_value_;
  };
  std::string string_value_;
};

std::map<std::string, Variant> AsVariantMap(
    const flatbuffers::Vector<flatbuffers::Offset<NamedVariant>>* extra);

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_UTILS_VARIANT_H_
