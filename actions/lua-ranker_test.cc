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

#include "actions/lua-ranker.h"

#include <string>

#include "actions/types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace libtextclassifier3 {
namespace {

MATCHER_P2(IsAction, type, response_text, "") {
  return testing::Value(arg.type, type) &&
         testing::Value(arg.response_text, response_text);
}

MATCHER_P(IsActionType, type, "") { return testing::Value(arg.type, type); }

TEST(LuaRankingTest, PassThrough) {
  ActionsSuggestionsResponse response;
  response.actions = {
      {/*response_text=*/"hello there", /*type=*/"text_reply",
       /*score=*/1.0},
      {/*response_text=*/"", /*type=*/"share_location", /*score=*/0.5},
      {/*response_text=*/"", /*type=*/"add_to_collection", /*score=*/0.1}};
  const std::string test_snippet = R"(
    local result = {}
    for i=1,#actions do
      table.insert(result, i)
    end
    return result
  )";

  EXPECT_TRUE(ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
                  test_snippet, &response)
                  ->RankActions());
  EXPECT_THAT(response.actions,
              testing::ElementsAreArray({IsActionType("text_reply"),
                                         IsActionType("share_location"),
                                         IsActionType("add_to_collection")}));
}

TEST(LuaRankingTest, Filtering) {
  ActionsSuggestionsResponse response;
  response.actions = {
      {/*response_text=*/"hello there", /*type=*/"text_reply",
       /*score=*/1.0},
      {/*response_text=*/"", /*type=*/"share_location", /*score=*/0.5},
      {/*response_text=*/"", /*type=*/"add_to_collection", /*score=*/0.1}};
  const std::string test_snippet = R"(
    return {}
  )";

  EXPECT_TRUE(ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
                  test_snippet, &response)
                  ->RankActions());
  EXPECT_THAT(response.actions, testing::IsEmpty());
}

TEST(LuaRankingTest, Duplication) {
  ActionsSuggestionsResponse response;
  response.actions = {
      {/*response_text=*/"hello there", /*type=*/"text_reply",
       /*score=*/1.0},
      {/*response_text=*/"", /*type=*/"share_location", /*score=*/0.5},
      {/*response_text=*/"", /*type=*/"add_to_collection", /*score=*/0.1}};
  const std::string test_snippet = R"(
    local result = {}
    for i=1,#actions do
      table.insert(result, 1)
    end
    return result
  )";

  EXPECT_TRUE(ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
                  test_snippet, &response)
                  ->RankActions());
  EXPECT_THAT(response.actions,
              testing::ElementsAreArray({IsActionType("text_reply"),
                                         IsActionType("text_reply"),
                                         IsActionType("text_reply")}));
}

TEST(LuaRankingTest, SortByScore) {
  ActionsSuggestionsResponse response;
  response.actions = {
      {/*response_text=*/"hello there", /*type=*/"text_reply",
       /*score=*/1.0},
      {/*response_text=*/"", /*type=*/"share_location", /*score=*/0.5},
      {/*response_text=*/"", /*type=*/"add_to_collection", /*score=*/0.1}};
  const std::string test_snippet = R"(
    function testScoreSorter(a, b)
      return actions[a].score < actions[b].score
    end
    local result = {}
    for i=1,#actions do
      result[i] = i
    end
    table.sort(result, testScoreSorter)
    return result
  )";

  EXPECT_TRUE(ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
                  test_snippet, &response)
                  ->RankActions());
  EXPECT_THAT(response.actions,
              testing::ElementsAreArray({IsActionType("add_to_collection"),
                                         IsActionType("share_location"),
                                         IsActionType("text_reply")}));
}

TEST(LuaRankingTest, SuppressType) {
  ActionsSuggestionsResponse response;
  response.actions = {
      {/*response_text=*/"hello there", /*type=*/"text_reply",
       /*score=*/1.0},
      {/*response_text=*/"", /*type=*/"share_location", /*score=*/0.5},
      {/*response_text=*/"", /*type=*/"add_to_collection", /*score=*/0.1}};
  const std::string test_snippet = R"(
    local result = {}
    for id, action in pairs(actions) do
      if action.type ~= "text_reply" then
        table.insert(result, id)
      end
    end
    return result
  )";

  EXPECT_TRUE(ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
                  test_snippet, &response)
                  ->RankActions());
  EXPECT_THAT(response.actions,
              testing::ElementsAreArray({IsActionType("share_location"),
                                         IsActionType("add_to_collection")}));
}

}  // namespace
}  // namespace libtextclassifier3
