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
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "utils/sentencepiece/double_array_trie.h"
#include "utils/sentencepiece/normalizer.h"
#include "utils/sentencepiece/test_utils.h"
#include "utils/strings/stringpiece.h"

namespace libtextclassifier3 {
namespace {

std::string GetTestConfigPath() {
  return "";
}

TEST(NormalizerTest, NormalizesAsReferenceNormalizer) {
  std::ifstream test_config_stream(GetTestConfigPath());
  std::string config((std::istreambuf_iterator<char>(test_config_stream)),
                     (std::istreambuf_iterator<char>()));
  SentencePieceNormalizer normalizer =
      NormalizerFromSpec(config, /*add_dummy_prefix=*/true,
                         /*remove_extra_whitespaces=*/true,
                         /*escape_whitespaces=*/true);

  EXPECT_EQ(normalizer.Normalize("hello there"), "▁hello▁there");

  // Redundant whitespace.
  EXPECT_EQ(normalizer.Normalize("when is  the  world cup?"),
            "▁when▁is▁the▁world▁cup?");

  // Different whitespace.
  EXPECT_EQ(normalizer.Normalize("general\tkenobi"), "▁general▁kenobi");

  // NFKC char to multi-char normalization.
  EXPECT_EQ(normalizer.Normalize("㍿"), "▁株式会社");

  // Half width katakana, character composition happens.
  EXPECT_EQ(normalizer.Normalize(" ｸﾞｰｸﾞﾙ "), "▁グーグル");

  // NFKC char to char normalization.
  EXPECT_EQ(normalizer.Normalize("①②③"), "▁123");
}

TEST(NormalizerTest, NoDummyPrefix) {
  std::ifstream test_config_stream(GetTestConfigPath());
  std::string config((std::istreambuf_iterator<char>(test_config_stream)),
                     (std::istreambuf_iterator<char>()));
  SentencePieceNormalizer normalizer =
      NormalizerFromSpec(config, /*add_dummy_prefix=*/false,
                         /*remove_extra_whitespaces=*/true,
                         /*escape_whitespaces=*/true);

  EXPECT_EQ(normalizer.Normalize("hello there"), "hello▁there");

  // Redundant whitespace.
  EXPECT_EQ(normalizer.Normalize("when is  the  world cup?"),
            "when▁is▁the▁world▁cup?");

  // Different whitespace.
  EXPECT_EQ(normalizer.Normalize("general\tkenobi"), "general▁kenobi");

  // NFKC char to multi-char normalization.
  EXPECT_EQ(normalizer.Normalize("㍿"), "株式会社");

  // Half width katakana, character composition happens.
  EXPECT_EQ(normalizer.Normalize(" ｸﾞｰｸﾞﾙ "), "グーグル");

  // NFKC char to char normalization.
  EXPECT_EQ(normalizer.Normalize("①②③"), "123");
}

TEST(NormalizerTest, NoRemoveExtraWhitespace) {
  std::ifstream test_config_stream(GetTestConfigPath());
  std::string config((std::istreambuf_iterator<char>(test_config_stream)),
                     (std::istreambuf_iterator<char>()));
  SentencePieceNormalizer normalizer =
      NormalizerFromSpec(config, /*add_dummy_prefix=*/false,
                         /*remove_extra_whitespaces=*/false,
                         /*escape_whitespaces=*/true);

  EXPECT_EQ(normalizer.Normalize("hello there"), "hello▁there");

  // Redundant whitespace.
  EXPECT_EQ(normalizer.Normalize("when is  the  world cup?"),
            "when▁is▁▁the▁▁world▁cup?");

  // Different whitespace.
  EXPECT_EQ(normalizer.Normalize("general\tkenobi"), "general▁kenobi");
}

TEST(NormalizerTest, NoEscapeWhitespaces) {
  std::ifstream test_config_stream(GetTestConfigPath());
  std::string config((std::istreambuf_iterator<char>(test_config_stream)),
                     (std::istreambuf_iterator<char>()));
  SentencePieceNormalizer normalizer =
      NormalizerFromSpec(config, /*add_dummy_prefix=*/false,
                         /*remove_extra_whitespaces=*/false,
                         /*escape_whitespaces=*/false);

  EXPECT_EQ(normalizer.Normalize("hello there"), "hello there");

  // Redundant whitespace.
  EXPECT_EQ(normalizer.Normalize("when is  the  world cup?"),
            "when is  the  world cup?");

  // Different whitespace.
  EXPECT_EQ(normalizer.Normalize("general\tkenobi"), "general kenobi");
}

}  // namespace
}  // namespace libtextclassifier3
