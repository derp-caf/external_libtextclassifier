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

#include "utils/flatbuffers.h"

#include <vector>
#include "utils/named-extra_generated.h"

namespace libtextclassifier3 {

template <>
const char* FlatbufferFileIdentifier<Model>() {
  return ModelIdentifier();
}

std::unique_ptr<ReflectiveFlatbuffer> ReflectiveFlatbufferBuilder::NewRoot()
    const {
  return std::unique_ptr<ReflectiveFlatbuffer>(
      new ReflectiveFlatbuffer(schema_, schema_->root_table()));
}

std::unique_ptr<ReflectiveFlatbuffer> ReflectiveFlatbufferBuilder::NewTable(
    StringPiece table_name) const {
  for (const reflection::Object* object : *schema_->objects()) {
    if (table_name.Equals(object->name()->str())) {
      return std::unique_ptr<ReflectiveFlatbuffer>(
          new ReflectiveFlatbuffer(schema_, object));
    }
  }
  return nullptr;
}

const reflection::Field* ReflectiveFlatbuffer::GetFieldOrNull(
    const StringPiece field_name) const {
  return type_->fields()->LookupByKey(field_name.data());
}

bool ReflectiveFlatbuffer::IsMatchingType(const reflection::Field* field,
                                          const Variant& value) const {
  switch (field->type()->base_type()) {
    case reflection::Bool:
      return value.HasBool();
    case reflection::Int:
      return value.HasInt();
    case reflection::Long:
      return value.HasInt64();
    case reflection::Float:
      return value.HasFloat();
    case reflection::Double:
      return value.HasDouble();
    case reflection::String:
      return value.HasString();
    default:
      return false;
  }
}

ReflectiveFlatbuffer* ReflectiveFlatbuffer::Mutable(
    const StringPiece field_name) {
  if (const reflection::Field* field = GetFieldOrNull(field_name)) {
    return Mutable(field);
  }
  TC3_LOG(ERROR) << "Unknown field: " << field_name.ToString();
  return nullptr;
}

ReflectiveFlatbuffer* ReflectiveFlatbuffer::Mutable(
    const reflection::Field* field) {
  if (field->type()->base_type() != reflection::Obj) {
    TC3_LOG(ERROR) << "Field is not of type Object.";
    return nullptr;
  }
  auto entry = children_.find(field->offset());
  if (entry != children_.end()) {
    return entry->second.get();
  }
  auto it = children_.insert(
      entry,
      std::make_pair(
          field->offset(),
          std::unique_ptr<ReflectiveFlatbuffer>(new ReflectiveFlatbuffer(
              schema_, schema_->objects()->Get(field->type()->index())))));
  return it->second.get();
}

flatbuffers::uoffset_t ReflectiveFlatbuffer::Serialize(
    flatbuffers::FlatBufferBuilder* builder) const {
  // Build all children before we can start with this table.
  std::vector<
      std::pair</* field vtable offset */ int,
                /* field data offset in buffer */ flatbuffers::uoffset_t>>
      offsets;
  offsets.reserve(children_.size());
  for (const auto& it : children_) {
    offsets.push_back({it.first, it.second->Serialize(builder)});
  }

  // Create strings.
  for (const auto& it : fields_) {
    if (it.second.HasString()) {
      offsets.push_back({it.first->offset(),
                         builder->CreateString(it.second.StringValue()).o});
    }
  }

  // Build the table now.
  const flatbuffers::uoffset_t table_start = builder->StartTable();

  // Add scalar fields.
  for (const auto& it : fields_) {
    switch (it.second.GetType()) {
      case VariantValue_::Type_BOOL_VALUE:
        builder->AddElement<uint8_t>(
            it.first->offset(), static_cast<uint8_t>(it.second.BoolValue()),
            static_cast<uint8_t>(it.first->default_integer()));
        continue;
      case VariantValue_::Type_INT_VALUE:
        builder->AddElement<int32>(
            it.first->offset(), it.second.IntValue(),
            static_cast<int32>(it.first->default_integer()));
        continue;
      case VariantValue_::Type_INT64_VALUE:
        builder->AddElement<int64>(it.first->offset(), it.second.Int64Value(),
                                   it.first->default_integer());
        continue;
      case VariantValue_::Type_FLOAT_VALUE:
        builder->AddElement<float>(
            it.first->offset(), it.second.FloatValue(),
            static_cast<float>(it.first->default_real()));
        continue;
      case VariantValue_::Type_DOUBLE_VALUE:
        builder->AddElement<double>(it.first->offset(), it.second.DoubleValue(),
                                    it.first->default_real());
        continue;
      default:
        continue;
    }
  }

  // Add strings and subtables.
  for (const auto& it : offsets) {
    builder->AddOffset(it.first, flatbuffers::Offset<void>(it.second));
  }

  return builder->EndTable(table_start);
}

}  // namespace libtextclassifier3
