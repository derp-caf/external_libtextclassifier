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

class ActionsSuggestionsTest : public testing::Test {
 protected:
  ActionsSuggestionsTest() : INIT_UNILIB_FOR_TESTING(unilib_) {}
  std::unique_ptr<ActionsSuggestions> LoadTestModel() {
    return ActionsSuggestions::FromPath(GetModelPath() + kModelFileName,
                                        &unilib_);
  }
  UniLib unilib_;
};

TEST_F(ActionsSuggestionsTest, InstantiateActionSuggestions) {
  EXPECT_THAT(LoadTestModel(), testing::NotNull());
}

TEST_F(ActionsSuggestionsTest, SuggestActions) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "Where are you?"}}});
  EXPECT_EQ(response.actions.size(), 4);
}

TEST_F(ActionsSuggestionsTest, SuggestActionsFromAnnotations) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  AnnotatedSpan annotation;
  annotation.span = {11, 15};
  annotation.classification = {ClassificationResult("address", 1.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "are you at home?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation}}}});
  EXPECT_EQ(response.actions.size(), 5);
  EXPECT_EQ(response.actions.front().type, "view_map");
  EXPECT_EQ(response.actions.front().score, 1.0);
}

void TestSuggestActionsWithThreshold(
    const std::function<void(ActionsModelT*)>& set_value_fn,
    const UniLib* unilib = nullptr, const int expected_size = 0) {
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
          builder.GetSize(), unilib);
  ASSERT_TRUE(actions_suggestions);
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "Where are you?"}}});
  EXPECT_EQ(response.actions.size(), expected_size);
}

TEST_F(ActionsSuggestionsTest, SuggestActionsWithTriggeringScore) {
  TestSuggestActionsWithThreshold(
      [](ActionsModelT* actions_model) {
        actions_model->preconditions->min_smart_reply_triggering_score = 1.0;
      },
      &unilib_,
      /*expected_size=*/1 /*no smart reply, only actions*/
  );
}

TEST_F(ActionsSuggestionsTest, SuggestActionsWithSensitiveTopicScore) {
  TestSuggestActionsWithThreshold(
      [](ActionsModelT* actions_model) {
        actions_model->preconditions->max_sensitive_topic_score = 0.0;
      },
      &unilib_,
      /*expected_size=*/4 /* no sensitive prediction in test model*/);
}

TEST_F(ActionsSuggestionsTest, SuggestActionsWithMaxInputLength) {
  TestSuggestActionsWithThreshold(
      [](ActionsModelT* actions_model) {
        actions_model->preconditions->max_input_length = 0;
      },
      &unilib_);
}

TEST_F(ActionsSuggestionsTest, SuggestActionsWithMinInputLength) {
  TestSuggestActionsWithThreshold(
      [](ActionsModelT* actions_model) {
        actions_model->preconditions->min_input_length = 100;
      },
      &unilib_);
}

TEST_F(ActionsSuggestionsTest, SuppressActionsFromAnnotationsOnSensitiveTopic) {
  const std::string actions_model_string =
      ReadFile(GetModelPath() + kModelFileName);
  std::unique_ptr<ActionsModelT> actions_model =
      UnPackActionsModel(actions_model_string.c_str());

  // Don't test if no sensitivity score is produced
  if (actions_model->tflite_model_spec->output_sensitive_topic_score < 0) {
    return;
  }

  actions_model->preconditions->max_sensitive_topic_score = 0.0;
  actions_model->preconditions->suppress_on_sensitive_topic = true;
  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  std::unique_ptr<ActionsSuggestions> actions_suggestions =
      ActionsSuggestions::FromUnownedBuffer(
          reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib_);
  AnnotatedSpan annotation;
  annotation.span = {11, 15};
  annotation.classification = {
      ClassificationResult(Annotator::kAddressCollection, 1.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "are you at home?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation}}}});
  EXPECT_THAT(response.actions, testing::IsEmpty());
}

TEST_F(ActionsSuggestionsTest, SuggestActionsWithLongerConversation) {
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
          builder.GetSize(), &unilib_);
  AnnotatedSpan annotation;
  annotation.span = {11, 15};
  annotation.classification = {
      ClassificationResult(Annotator::kAddressCollection, 1.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/0, "hi, how are you?", /*reference_time=*/10000},
            {/*user_id=*/1, "good! are you at home?",
             /*reference_time=*/15000,
             /*annotations=*/{annotation}}}});
  EXPECT_EQ(response.actions.size(), 1);
  EXPECT_EQ(response.actions.back().type, "view_map");
  EXPECT_EQ(response.actions.back().score, 1.0);
}

TEST_F(ActionsSuggestionsTest, CreateActionsFromClassificationResult) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  AnnotatedSpan annotation;
  annotation.span = {13, 16};
  annotation.classification = {
      ClassificationResult(Annotator::kPhoneCollection, 1.0)};

  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "can you call 911?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation}}}});

  EXPECT_EQ(response.actions.size(),
            5 /* smart replies + actions from annotations*/);
  EXPECT_EQ(response.actions[0].type, "call_phone");
  EXPECT_EQ(response.actions[0].score, 1.0);
  EXPECT_EQ(response.actions[0].annotations.size(), 1);
  EXPECT_EQ(response.actions[0].annotations[0].message_index, 0);
  EXPECT_EQ(response.actions[0].annotations[0].span, annotation.span);
  EXPECT_EQ(response.actions[1].type, "send_sms");
  EXPECT_EQ(response.actions[1].score, 1.0);
  EXPECT_EQ(response.actions[1].annotations.size(), 1);
  EXPECT_EQ(response.actions[1].annotations[0].message_index, 0);
  EXPECT_EQ(response.actions[1].annotations[0].span, annotation.span);
}

#ifdef TC3_UNILIB_ICU
TEST_F(ActionsSuggestionsTest, CreateActionsFromRules) {
  const std::string actions_model_string =
      ReadFile(GetModelPath() + kModelFileName);
  std::unique_ptr<ActionsModelT> actions_model =
      UnPackActionsModel(actions_model_string.c_str());

  actions_model->rules.reset(new RulesModelT());
  actions_model->rules->rule.emplace_back(new RulesModel_::RuleT);
  actions_model->rules->rule.back()->pattern = "^(?i:hello\\sthere)$";
  actions_model->rules->rule.back()->actions.emplace_back(
      new ActionSuggestionSpecT);
  actions_model->rules->rule.back()->actions.back()->type = "text_reply";
  actions_model->rules->rule.back()->actions.back()->response_text =
      "General Kenobi!";
  actions_model->rules->rule.back()->actions.back()->score = 1.f;

  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  std::unique_ptr<ActionsSuggestions> actions_suggestions =
      ActionsSuggestions::FromUnownedBuffer(
          reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib_);

  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "hello there"}}});
  EXPECT_EQ(response.actions.size(), 1);
  EXPECT_EQ(response.actions[0].response_text, "General Kenobi!");
}

TEST_F(ActionsSuggestionsTest, DeduplicateActions) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  ActionsSuggestionsResponse response = actions_suggestions->SuggestActions(
      {{{/*user_id=*/1, "Where are you?"}}});
  EXPECT_EQ(response.actions.size(), 4);

  // Check that the location sharing model triggered.
  bool has_location_sharing_action = false;
  for (const ActionSuggestion action : response.actions) {
    if (action.type == ActionsSuggestions::kShareLocation) {
      has_location_sharing_action = true;
      break;
    }
  }
  EXPECT_TRUE(has_location_sharing_action);

  // Add custom rule for location sharing.
  const std::string actions_model_string =
      ReadFile(GetModelPath() + kModelFileName);
  std::unique_ptr<ActionsModelT> actions_model =
      UnPackActionsModel(actions_model_string.c_str());

  actions_model->rules.reset(new RulesModelT());
  actions_model->rules->rule.emplace_back(new RulesModel_::RuleT);
  actions_model->rules->rule.back()->pattern = "^(?i:where are you[.?]?)$";
  actions_model->rules->rule.back()->actions.emplace_back(
      new ActionSuggestionSpecT);
  actions_model->rules->rule.back()->actions.back()->type = "text_reply";
  actions_model->rules->rule.back()->actions.back()->response_text =
      "I am already here for the test!";
  actions_model->rules->rule.back()->actions.back()->score = 1.f;
  actions_model->rules->rule.back()->actions.back()->type =
      ActionsSuggestions::kShareLocation;

  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  actions_suggestions = ActionsSuggestions::FromUnownedBuffer(
      reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
      builder.GetSize(), &unilib_);

  response = actions_suggestions->SuggestActions(
      {{{/*user_id=*/1, "Where are you?"}}});
  EXPECT_EQ(response.actions.size(), 5);
}
#endif

}  // namespace
}  // namespace libtextclassifier3
