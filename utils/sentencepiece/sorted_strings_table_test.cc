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

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "utils/sentencepiece/sorted_strings_table.h"

namespace libtextclassifier3 {
namespace {

TEST(SortedStringsTest, Lookup) {
  const char pieces[] = "hell\0hello\0o\0there\0";
  const int offsets[] = {0, 5, 11, 13};

  SortedStringsTable table(/*num_pieces=*/4, offsets, StringPiece(pieces, 18),
                           /*use_linear_scan_threshold=*/1);

  auto matches = table.FindAllPrefixMatches("hello there");
  EXPECT_EQ(matches.size(), 2);
  EXPECT_EQ(matches[0].id, 0 /*hell*/);
  EXPECT_EQ(matches[0].match_length, 4 /*hell*/);
  EXPECT_EQ(matches[1].id, 1 /*hello*/);
  EXPECT_EQ(matches[1].match_length, 5 /*hello*/);

  matches = table.FindAllPrefixMatches("he");
  EXPECT_EQ(matches.size(), 0);

  matches = table.FindAllPrefixMatches("abcd");
  EXPECT_EQ(matches.size(), 0);

  matches = table.FindAllPrefixMatches("");
  EXPECT_EQ(matches.size(), 0);

  EXPECT_THAT(table.FindAllPrefixMatches("hi there"), testing::IsEmpty());

  EXPECT_EQ(table.LongestPrefixMatch("hella there").id, 0 /*hell*/);
  EXPECT_EQ(table.LongestPrefixMatch("hello there").id, 1 /*hello*/);
  EXPECT_EQ(table.LongestPrefixMatch("abcd").id, -1);
  EXPECT_EQ(table.LongestPrefixMatch("").id, -1);
}

}  // namespace
}  // namespace libtextclassifier3
