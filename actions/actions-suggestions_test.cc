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

#include "actions/actions-suggestions.h"

#include <fstream>
#include <iterator>
#include <memory>

#include "actions/actions_model_generated.h"
#include "annotator/types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "flatbuffers/flatbuffers.h"

namespace libtextclassifier3 {
namespace {

constexpr char kModelFileName[] = "actions_suggestions_test.model";

std::string ReadFile(const std::string& file_name) {
  std::ifstream file_stream(file_name);
  return std::string(std::istreambuf_iterator<char>(file_stream), {});
}

std::string GetModelPath() {
  return "";
}

std::unique_ptr<ActionsSuggestions> LoadTestModel() {
  return ActionsSuggestions::FromPath(GetModelPath() + kModelFileName);
}

TEST(ActionsSuggestionsTest, InstantiateActionSuggestions) {
  EXPECT_THAT(LoadTestModel(), testing::NotNull());
}

TEST(ActionsSuggestionsTest, SuggestActions) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  const std::vector<ActionSuggestion>& actions =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "where are you?"}}});
  EXPECT_EQ(actions.size(), 6);
}

TEST(ActionsSuggestionsTest, SuggestActionsFromAnnotations) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  AnnotatedSpan annotation;
  annotation.span = {11, 15};
  annotation.classification = {ClassificationResult("address", 1.0)};
  const std::vector<ActionSuggestion>& actions =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "are you at home?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation}}}});
  EXPECT_EQ(actions.size(), 7);
  EXPECT_EQ(actions.back().type, "address");
  EXPECT_EQ(actions.back().score, 1.0);
}

void TestSuggestActionsWithThreshold(
    const std::function<void(ActionsModelT*)>& set_value_fn) {
  const std::string actions_model_string =
      ReadFile(GetModelPath() + kModelFileName);
  std::unique_ptr<ActionsModelT> actions_model =
      UnPackActionsModel(actions_model_string.c_str());
  set_value_fn(actions_model.get());
  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  std::unique_ptr<ActionsSuggestions> actions_suggestions =
      ActionsSuggestions::FromUnownedBuffer(
          reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
          builder.GetSize());
  ASSERT_TRUE(actions_suggestions);
  const std::vector<ActionSuggestion>& actions =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "where are you?"}}});
  EXPECT_THAT(actions, testing::IsEmpty());
}

TEST(ActionsSuggestionsTest, SuggestActionsWithTriggeringScore) {
  TestSuggestActionsWithThreshold([](ActionsModelT* actions_model) {
    actions_model->min_triggering_confidence = 1.0;
  });
}

TEST(ActionsSuggestionsTest, SuggestActionsWithSensitiveTopicScore) {
  TestSuggestActionsWithThreshold([](ActionsModelT* actions_model) {
    actions_model->max_sensitive_topic_score = 0.0;
  });
}

TEST(ActionsSuggestionsTest, SuggestActionsWithLongerConversation) {
  const std::string actions_model_string =
      ReadFile(GetModelPath() + kModelFileName);
  std::unique_ptr<ActionsModelT> actions_model =
      UnPackActionsModel(actions_model_string.c_str());

  // Allow a larger conversation context.
  actions_model->max_conversation_history_length = 10;

  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  std::unique_ptr<ActionsSuggestions> actions_suggestions =
      ActionsSuggestions::FromUnownedBuffer(
          reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
          builder.GetSize());
  AnnotatedSpan annotation;
  annotation.span = {11, 15};
  annotation.classification = {ClassificationResult("address", 1.0)};
  const std::vector<ActionSuggestion>& actions =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/0, "hi, how are you?", /*time_diff_secs=*/0},
            {/*user_id=*/1, "good! are you at home?",
             /*time_diff_secs=*/60,
             /*annotations=*/{annotation}}}});
  EXPECT_EQ(actions.size(), 7);
  EXPECT_EQ(actions.back().type, "address");
  EXPECT_EQ(actions.back().score, 1.0);
}

}  // namespace
}  // namespace libtextclassifier3
