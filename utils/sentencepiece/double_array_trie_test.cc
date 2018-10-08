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
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "utils/sentencepiece/double_array_trie.h"

namespace libtextclassifier3 {
namespace {

std::string GetTestConfigPath() {
  return "";
}

TEST(DoubleArrayTest, Lookup) {
  // Test trie that contains pieces "hell", "hello", "o", "there".
  std::ifstream test_config_stream(GetTestConfigPath());
  std::string config((std::istreambuf_iterator<char>(test_config_stream)),
                     (std::istreambuf_iterator<char>()));
  DoubleArrayTrie trie(reinterpret_cast<const TrieNode*>(config.data()),
                       config.size() / sizeof(TrieNode));

  auto matches = trie.FindAllPrefixMatches("hello there");
  EXPECT_EQ(matches.size(), 2);
  EXPECT_EQ(matches[0].id, 0 /*hell*/);
  EXPECT_EQ(matches[0].match_length, 4 /*hell*/);
  EXPECT_EQ(matches[1].id, 1 /*hello*/);
  EXPECT_EQ(matches[1].match_length, 5 /*hello*/);

  matches = trie.FindAllPrefixMatches("he");
  EXPECT_EQ(matches.size(), 0);

  matches = trie.FindAllPrefixMatches("abcd");
  EXPECT_EQ(matches.size(), 0);

  matches = trie.FindAllPrefixMatches("");
  EXPECT_EQ(matches.size(), 0);

  EXPECT_THAT(trie.FindAllPrefixMatches("hi there"), testing::IsEmpty());

  EXPECT_EQ(trie.LongestPrefixMatch("hella there").id, 0 /*hell*/);
  EXPECT_EQ(trie.LongestPrefixMatch("hello there").id, 1 /*hello*/);
  EXPECT_EQ(trie.LongestPrefixMatch("abcd").id, -1);
  EXPECT_EQ(trie.LongestPrefixMatch("").id, -1);
}

}  // namespace
}  // namespace libtextclassifier3
