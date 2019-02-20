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

#include "utils/resources.h"
#include "utils/i18n/locale.h"
#include "utils/resources_generated.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace libtextclassifier3 {
namespace {

std::string BuildTestResources() {
  ResourcePoolT test_resources;

  // Test locales.
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "en";
  test_resources.locale.back()->region = "US";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "en";
  test_resources.locale.back()->region = "GB";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "de";
  test_resources.locale.back()->region = "DE";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "fr";
  test_resources.locale.back()->region = "FR";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "pt";
  test_resources.locale.back()->region = "PT";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "pt";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "zh";
  test_resources.locale.back()->script = "Hans";
  test_resources.locale.back()->region = "CN";
  test_resources.locale.emplace_back(new LanguageTagT);
  test_resources.locale.back()->language = "zh";

  // Test entries.
  test_resources.resource_entry.emplace_back(new ResourceEntryT);
  test_resources.resource_entry.back()->name = /*resource_name=*/"A";

  // en-US
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "localize";
  test_resources.resource_entry.back()->resource.back()->locale = 0;

  // en-GB
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "localise";
  test_resources.resource_entry.back()->resource.back()->locale = 1;

  // de-DE
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content =
      "lokalisieren";
  test_resources.resource_entry.back()->resource.back()->locale = 2;

  // fr-FR
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "localiser";
  test_resources.resource_entry.back()->resource.back()->locale = 3;

  // pt-PT
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "localizar";
  test_resources.resource_entry.back()->resource.back()->locale = 4;

  // pt
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "concentrar";
  test_resources.resource_entry.back()->resource.back()->locale = 5;

  // zh-Hans-CN
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "龙";
  test_resources.resource_entry.back()->resource.back()->locale = 6;

  // zh
  test_resources.resource_entry.back()->resource.emplace_back(new ResourceT);
  test_resources.resource_entry.back()->resource.back()->content = "龍";
  test_resources.resource_entry.back()->resource.back()->locale = 7;

  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(ResourcePool::Pack(builder, &test_resources));

  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                     builder.GetSize());
}

TEST(ResourcesTest, CorrectlyHandlesExactMatch) {
  std::string test_resources = BuildTestResources();
  Resources resources(
      flatbuffers::GetRoot<ResourcePool>(test_resources.data()));
  StringPiece s;
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("en-US"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("localize", s.ToString());
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("en-GB"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("localise", s.ToString());
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("pt-PT"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("localizar", s.ToString());
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh-Hans-CN"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龙", s.ToString());
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龍", s.ToString());
}

TEST(ResourcesTest, CorrectlyHandlesTie) {
  std::string test_resources = BuildTestResources();
  Resources resources(
      flatbuffers::GetRoot<ResourcePool>(test_resources.data()));
  // Uses first best match in case of a tie.
  StringPiece s;
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("en-CA"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("localize", s.ToString());
}

TEST(ResourcesTest, RequiresLanguageMatch) {
  std::string test_resources = BuildTestResources();
  Resources resources(
      flatbuffers::GetRoot<ResourcePool>(test_resources.data()));
  EXPECT_FALSE(resources.GetResourceContent(Locale::FromBCP47("es-US"),
                                            /*resource_name=*/"A",
                                            /*result=*/nullptr));
}

TEST(ResourcesTest, HandlesFallback) {
  std::string test_resources = BuildTestResources();
  Resources resources(
      flatbuffers::GetRoot<ResourcePool>(test_resources.data()));
  StringPiece s;
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("fr-CH"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("localiser", s.ToString());
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh-Hans"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龙", s.ToString());
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh-Hans-ZZ"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龙", s.ToString());
}

TEST(ResourcesTest, PreferGenericCallback) {
  std::string test_resources = BuildTestResources();
  Resources resources(
      flatbuffers::GetRoot<ResourcePool>(test_resources.data()));
  StringPiece s;
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("pt-BR"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("concentrar", s.ToString());  // Falls back to pt, not pt-PT.
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh-Hant"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龍", s.ToString());  // Falls back to zh, not zh-Hans-CN.
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh-Hant-CN"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龍", s.ToString());  // Falls back to zh, not zh-Hans-CN.
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("zh-CN"),
                                           /*resource_name=*/"A", &s));
  EXPECT_EQ("龍", s.ToString());  // Falls back to zh, not zh-Hans-CN.
}

TEST(ResourcesTest, PreferGenericWhenGeneric) {
  std::string test_resources = BuildTestResources();
  Resources resources(
      flatbuffers::GetRoot<ResourcePool>(test_resources.data()));
  StringPiece s;
  EXPECT_TRUE(resources.GetResourceContent(Locale::FromBCP47("pt"),
                                           /*resource_name=*/"A", &s));

  // Uses pt, not pt-PT.
  EXPECT_EQ("concentrar", s.ToString());
}

}  // namespace
}  // namespace libtextclassifier3
