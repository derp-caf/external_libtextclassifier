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

// Utility functions for working with FlatBuffers.

#ifndef LIBTEXTCLASSIFIER_UTILS_FLATBUFFERS_H_
#define LIBTEXTCLASSIFIER_UTILS_FLATBUFFERS_H_

#include <map>
#include <memory>
#include <string>

#include "annotator/model_generated.h"
#include "utils/strings/stringpiece.h"
#include "utils/variant.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/reflection.h"

namespace libtextclassifier3 {

// Loads and interprets the buffer as 'FlatbufferMessage' and verifies its
// integrity.
template <typename FlatbufferMessage>
const FlatbufferMessage* LoadAndVerifyFlatbuffer(const void* buffer, int size) {
  const FlatbufferMessage* message =
      flatbuffers::GetRoot<FlatbufferMessage>(buffer);
  if (message == nullptr) {
    return nullptr;
  }
  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(buffer),
                                 size);
  if (message->Verify(verifier)) {
    return message;
  } else {
    return nullptr;
  }
}

// Same as above but takes string.
template <typename FlatbufferMessage>
const FlatbufferMessage* LoadAndVerifyFlatbuffer(const std::string& buffer) {
  return LoadAndVerifyFlatbuffer<FlatbufferMessage>(buffer.c_str(),
                                                    buffer.size());
}

// Loads and interprets the buffer as 'FlatbufferMessage', verifies its
// integrity and returns its mutable version.
template <typename FlatbufferMessage>
std::unique_ptr<typename FlatbufferMessage::NativeTableType>
LoadAndVerifyMutableFlatbuffer(const void* buffer, int size) {
  const FlatbufferMessage* message =
      LoadAndVerifyFlatbuffer<FlatbufferMessage>(buffer, size);
  if (message == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<typename FlatbufferMessage::NativeTableType>(
      message->UnPack());
}

// Same as above but takes string.
template <typename FlatbufferMessage>
std::unique_ptr<typename FlatbufferMessage::NativeTableType>
LoadAndVerifyMutableFlatbuffer(const std::string& buffer) {
  return LoadAndVerifyMutableFlatbuffer<FlatbufferMessage>(buffer.c_str(),
                                                           buffer.size());
}

template <typename FlatbufferMessage>
const char* FlatbufferFileIdentifier() {
  return nullptr;
}

template <>
const char* FlatbufferFileIdentifier<Model>();

// Packs the mutable flatbuffer message to string.
template <typename FlatbufferMessage>
std::string PackFlatbuffer(
    const typename FlatbufferMessage::NativeTableType* mutable_message) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(FlatbufferMessage::Pack(builder, mutable_message),
                 FlatbufferFileIdentifier<FlatbufferMessage>());
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                     builder.GetSize());
}

// A flatbuffer that can be built using flatbuffer reflection data of the
// schema.
// Normally, field information is hard-coded in code generated from a flatbuffer
// schema. Here we lookup the necessary information for building a flatbuffer
// from the provided reflection meta data.
// When serializing a flatbuffer, the library requires that the sub messages
// are already serialized, therefore we explicitly keep the field values and
// serialize the message in (reverse) topological dependency order.
class ReflectiveFlatbuffer {
 public:
  ReflectiveFlatbuffer(const reflection::Schema* schema,
                       const reflection::Object* type)
      : schema_(schema), type_(type) {}

  // Gets the field information for a field name, returns nullptr if the
  // field was not defined.
  const reflection::Field* GetFieldOrNull(const StringPiece field_name) const;

  // Checks whether a variant value type agrees with a field type.
  bool IsMatchingType(const reflection::Field* field,
                      const Variant& value) const;

  // Sets a (primitive) field to a specific value.
  // Returns true if successful, and false if the field was not found or the
  // expected type doesn't match.
  template <typename T>
  bool Set(StringPiece field_name, T value) {
    if (const reflection::Field* field = GetFieldOrNull(field_name)) {
      return Set<T>(field, value);
    }
    return false;
  }

  // Sets a (primitive) field to a specific value.
  // Returns true if successful, and false if the expected type doesn't match.
  // Expects `field` to be non-null.
  template <typename T>
  bool Set(const reflection::Field* field, T value) {
    if (field == nullptr) {
      TC3_LOG(ERROR) << "Expected non-null field.";
      return false;
    }
    Variant variant_value(value);
    if (!IsMatchingType(field, variant_value)) {
      TC3_LOG(ERROR) << "Type mismatch for field `" << field->name()->str()
                     << "`, expected: " << field->type()->base_type()
                     << ", got: " << variant_value.GetType();
      return false;
    }
    fields_[field] = variant_value;
    return true;
  }

  // Gets the reflective flatbuffer for a table field.
  // Returns nullptr if the field was not found, or the field type was not a
  // table.
  ReflectiveFlatbuffer* Mutable(StringPiece field_name);
  ReflectiveFlatbuffer* Mutable(const reflection::Field* field);

  // Serializes the flatbuffer.
  flatbuffers::uoffset_t Serialize(
      flatbuffers::FlatBufferBuilder* builder) const;

 private:
  const reflection::Schema* const schema_;
  const reflection::Object* const type_;

  // Cached primitive fields (scalars and strings).
  std::map<const reflection::Field*, Variant> fields_;

  // Cached sub-messages, keyed by vtable offset.
  std::map<int, std::unique_ptr<ReflectiveFlatbuffer>> children_;
};

// A helper class to build flatbuffers based on schema reflection data.
// Can be used to a `ReflectiveFlatbuffer` for the root message of the
// schema, or any defined table via name.
class ReflectiveFlatbufferBuilder {
 public:
  explicit ReflectiveFlatbufferBuilder(const reflection::Schema* schema)
      : schema_(schema) {}

  // Starts a new root table message.
  std::unique_ptr<ReflectiveFlatbuffer> NewRoot() const;

  // Starts a new table message. Returns nullptr if no table with given name is
  // found in the schema.
  std::unique_ptr<ReflectiveFlatbuffer> NewTable(
      const StringPiece table_name) const;

 private:
  const reflection::Schema* const schema_;
};

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_UTILS_FLATBUFFERS_H_
