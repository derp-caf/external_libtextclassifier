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

#include <fstream>
#include <memory>
#include <string>

#include "utils/flatbuffers.h"
#include "utils/flatbuffers_test_generated.h"
#include "gtest/gtest.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/reflection.h"
#include "flatbuffers/reflection_generated.h"

namespace libtextclassifier3 {
namespace {

std::string GetTestMetadataPath() {
  return "flatbuffers_test.bfbs";
}

std::string LoadTestMetadata() {
  std::ifstream test_config_stream(GetTestMetadataPath());
  return std::string((std::istreambuf_iterator<char>(test_config_stream)),
                     (std::istreambuf_iterator<char>()));
}

TEST(FlatbuffersTest, ReflectionPrimitiveType) {
  std::string metadata_buffer = LoadTestMetadata();
  ReflectiveFlatbufferBuilder reflective_builder(
      flatbuffers::GetRoot<reflection::Schema>(metadata_buffer.data()));

  std::unique_ptr<ReflectiveFlatbuffer> buffer = reflective_builder.NewRoot();
  EXPECT_TRUE(buffer != nullptr);
  EXPECT_TRUE(buffer->Set("an_int_field", 42));
  EXPECT_TRUE(buffer->Set("a_long_field", 84ll));
  EXPECT_TRUE(buffer->Set("a_bool_field", true));
  EXPECT_TRUE(buffer->Set("a_float_field", 1.f));
  EXPECT_TRUE(buffer->Set("a_double_field", 1.0));

  // Try to parse with the generated code.
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(flatbuffers::Offset<void>(buffer->Serialize(&builder)));
  std::unique_ptr<test::ExtraT> extra =
      LoadAndVerifyMutableFlatbuffer<test::Extra>(builder.GetBufferPointer(),
                                                  builder.GetSize());
  EXPECT_TRUE(extra != nullptr);
  EXPECT_EQ(extra->an_int_field, 42);
  EXPECT_EQ(extra->a_long_field, 84);
  EXPECT_EQ(extra->a_bool_field, true);
  EXPECT_NEAR(extra->a_float_field, 1.f, 1e-4);
  EXPECT_NEAR(extra->a_double_field, 1.f, 1e-4);
}

TEST(FlatbuffersTest, ReflectionUnknownField) {
  std::string metadata_buffer = LoadTestMetadata();
  const reflection::Schema* schema =
      flatbuffers::GetRoot<reflection::Schema>(metadata_buffer.data());
  ReflectiveFlatbufferBuilder reflective_builder(schema);

  std::unique_ptr<ReflectiveFlatbuffer> buffer = reflective_builder.NewRoot();
  EXPECT_TRUE(buffer != nullptr);

  // Add a field that is not known to the (statically generated) code.
  EXPECT_TRUE(buffer->Set("mystic", "this is an unknown field."));

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(flatbuffers::Offset<void>(buffer->Serialize(&builder)));

  // Try to read the field again.
  const flatbuffers::Table* extra =
      flatbuffers::GetAnyRoot(builder.GetBufferPointer());
  EXPECT_EQ(extra
                ->GetPointer<const flatbuffers::String*>(
                    buffer->GetFieldOrNull("mystic")->offset())
                ->str(),
            "this is an unknown field.");
}

TEST(FlatbuffersTest, ReflectionRecursive) {
  std::string metadata_buffer = LoadTestMetadata();
  ReflectiveFlatbufferBuilder reflective_builder(
      flatbuffers::GetRoot<reflection::Schema>(metadata_buffer.data()));

  std::unique_ptr<ReflectiveFlatbuffer> buffer = reflective_builder.NewRoot();
  ReflectiveFlatbuffer* flight_info = buffer->Mutable("flight_number");
  flight_info->Set("carrier_code", "LX");
  flight_info->Set("flight_code", 38);

  ReflectiveFlatbuffer* contact_info = buffer->Mutable("contact_info");
  EXPECT_TRUE(contact_info->Set("first_name", "Barack"));
  EXPECT_TRUE(contact_info->Set("last_name", "Obama"));
  EXPECT_TRUE(contact_info->Set("phone_number", "1-800-TEST"));
  EXPECT_TRUE(contact_info->Set("score", 1.f));

  // Try to parse with the generated code.
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(flatbuffers::Offset<void>(buffer->Serialize(&builder)));
  std::unique_ptr<test::ExtraT> extra =
      LoadAndVerifyMutableFlatbuffer<test::Extra>(builder.GetBufferPointer(),
                                                  builder.GetSize());
  EXPECT_TRUE(extra != nullptr);
  EXPECT_EQ(extra->flight_number->carrier_code, "LX");
  EXPECT_EQ(extra->flight_number->flight_code, 38);
  EXPECT_EQ(extra->contact_info->first_name, "Barack");
  EXPECT_EQ(extra->contact_info->last_name, "Obama");
  EXPECT_EQ(extra->contact_info->phone_number, "1-800-TEST");
  EXPECT_NEAR(extra->contact_info->score, 1.f, 1e-4);
}

}  // namespace
}  // namespace libtextclassifier3
