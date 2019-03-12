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
#include "annotator/collections.h"
#include "annotator/types.h"
#include "utils/flatbuffers.h"
#include "utils/flatbuffers_generated.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/reflection.h"

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
          {{{/*user_id=*/1, "Where are you?", /*time_diff_secs=*/0,
             /*annotations=*/{}, /*locales=*/"en"}}});
  EXPECT_EQ(response.actions.size(), 4);
}

TEST_F(ActionsSuggestionsTest, SuggestNoActionsForUnknownLocale) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "Where are you?", /*time_diff_secs=*/0,
             /*annotations=*/{}, /*locales=*/"zz"}}});
  EXPECT_THAT(response.actions, testing::IsEmpty());
}

TEST_F(ActionsSuggestionsTest, SuggestActionsFromAnnotations) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  AnnotatedSpan annotation;
  annotation.span = {11, 15};
  annotation.classification = {ClassificationResult("address", 1.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "are you at home?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation},
                                             /*locales=*/"en"}}});
  EXPECT_EQ(response.actions.size(), 5);
  EXPECT_EQ(response.actions.front().type, "view_map");
  EXPECT_EQ(response.actions.front().score, 1.0);
}

TEST_F(ActionsSuggestionsTest, SuggestActionsFromDuplicatedAnnotations) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  AnnotatedSpan phone_annotation;
  phone_annotation.span = {11, 21};
  phone_annotation.classification = {ClassificationResult("phone", 2.5)};
  AnnotatedSpan phone_annotation2;
  phone_annotation2.span = {41, 51};
  phone_annotation2.classification = {ClassificationResult("phone", 3.0)};
  AnnotatedSpan email_annotation;
  email_annotation.span = {55, 68};
  email_annotation.classification = {ClassificationResult("email", 2.0)};

  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1,
             "call me at 1-800-TEST or send message to 1-800-TEST or "
             "test@test.com.",
             /*time_diff_secs=*/0,
             /*annotations=*/
             {phone_annotation, phone_annotation2, email_annotation},
             /*locales=*/"en"}}});

  EXPECT_GE(response.actions.size(), 5);
  EXPECT_EQ(response.actions[0].type, "call_phone");
  EXPECT_EQ(response.actions[0].score, 3.0);
  EXPECT_EQ(response.actions[1].type, "send_sms");
  EXPECT_EQ(response.actions[1].score, 3.0);
  EXPECT_EQ(response.actions[2].type, "add_contact");
  EXPECT_EQ(response.actions[2].score, 3.0);
  EXPECT_EQ(response.actions[3].type, "send_email");
  EXPECT_EQ(response.actions[3].score, 2.0);
  EXPECT_EQ(response.actions[4].type, "add_contact");
  EXPECT_EQ(response.actions[4].score, 2.0);
}

TEST_F(ActionsSuggestionsTest, SuggestActionsAnnotationsNoDeduplication) {
  const std::string actions_model_string =
      ReadFile(GetModelPath() + kModelFileName);
  std::unique_ptr<ActionsModelT> actions_model =
      UnPackActionsModel(actions_model_string.c_str());
  // Disable deduplication.
  actions_model->annotation_actions_spec->deduplicate_annotations = false;
  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  std::unique_ptr<ActionsSuggestions> actions_suggestions =
      ActionsSuggestions::FromUnownedBuffer(
          reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib_);
  AnnotatedSpan phone_annotation;
  phone_annotation.span = {11, 21};
  phone_annotation.classification = {ClassificationResult("phone", 2.5)};
  AnnotatedSpan phone_annotation2;
  phone_annotation2.span = {41, 51};
  phone_annotation2.classification = {ClassificationResult("phone", 3.0)};
  AnnotatedSpan email_annotation;
  email_annotation.span = {55, 68};
  email_annotation.classification = {ClassificationResult("email", 2.0)};

  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1,
             "call me at 1-800-TEST or send message to 1-800-TEST or "
             "test@test.com.",
             /*time_diff_secs=*/0,
             /*annotations=*/
             {phone_annotation, phone_annotation2, email_annotation},
             /*locales=*/"en"}}});

  EXPECT_GE(response.actions.size(), 8);
  EXPECT_EQ(response.actions[0].type, "call_phone");
  EXPECT_EQ(response.actions[0].score, 3.0);
  EXPECT_EQ(response.actions[1].type, "send_sms");
  EXPECT_EQ(response.actions[1].score, 3.0);
  EXPECT_EQ(response.actions[2].type, "add_contact");
  EXPECT_EQ(response.actions[2].score, 3.0);
  EXPECT_EQ(response.actions[3].type, "call_phone");
  EXPECT_EQ(response.actions[3].score, 2.5);
  EXPECT_EQ(response.actions[4].type, "send_sms");
  EXPECT_EQ(response.actions[4].score, 2.5);
  EXPECT_EQ(response.actions[5].type, "add_contact");
  EXPECT_EQ(response.actions[5].score, 2.5);
  EXPECT_EQ(response.actions[6].type, "send_email");
  EXPECT_EQ(response.actions[6].score, 2.0);
  EXPECT_EQ(response.actions[7].type, "add_contact");
  EXPECT_EQ(response.actions[7].score, 2.0);
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
          {{{/*user_id=*/1, "I have the low-ground. Where are you?",
             /*time_diff_secs=*/0,
             /*annotations=*/{}, /*locales=*/"en"}}});
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

#ifdef TC3_UNILIB_ICU
TEST_F(ActionsSuggestionsTest, SuggestActionsLowConfidence) {
  TestSuggestActionsWithThreshold(
      [](ActionsModelT* actions_model) {
        actions_model->preconditions->suppress_on_low_confidence_input = true;
        actions_model->preconditions->low_confidence_rules.reset(
            new RulesModelT);
        actions_model->preconditions->low_confidence_rules->rule.emplace_back(
            new RulesModel_::RuleT);
        actions_model->preconditions->low_confidence_rules->rule.back()
            ->pattern = "low-ground";
      },
      &unilib_);
}
#endif

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
      ClassificationResult(Collections::Address(), 1.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "are you at home?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation},
                                             /*locales=*/"en"}}});
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
      ClassificationResult(Collections::Address(), 1.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/0, "hi, how are you?", /*reference_time=*/10000,
             /*annotations=*/{}, /*locales=*/"en"},
            {/*user_id=*/1, "good! are you at home?",
             /*reference_time=*/15000,
             /*annotations=*/{annotation},
             /*locales=*/"en"}}});
  EXPECT_EQ(response.actions.size(), 1);
  EXPECT_EQ(response.actions.back().type, "view_map");
  EXPECT_EQ(response.actions.back().score, 1.0);
}

TEST_F(ActionsSuggestionsTest, CreateActionsFromClassificationResult) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  AnnotatedSpan annotation;
  annotation.span = {13, 16};
  annotation.classification = {ClassificationResult(Collections::Phone(), 1.0)};

  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions({{{/*user_id=*/1, "can you call 911?",
                                             /*time_diff_secs=*/0,
                                             /*annotations=*/{annotation},
                                             /*locales=*/"en"}}});

  EXPECT_EQ(response.actions.size(),
            6 /* smart replies + actions from annotations*/);
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
  EXPECT_EQ(response.actions[2].type, "add_contact");
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
  RulesModel_::RuleT* rule = actions_model->rules->rule.back().get();
  rule->pattern = "^(?i:hello\\s(there))$";
  rule->actions.emplace_back(new RulesModel_::Rule_::RuleActionSpecT);
  rule->actions.back()->action.reset(new ActionSuggestionSpecT);
  ActionSuggestionSpecT* action = rule->actions.back()->action.get();
  action->type = "text_reply";
  action->response_text = "General Kenobi!";
  action->score = 1.f;

  // Set capturing groups for entity data.
  rule->actions.back()->capturing_group.emplace_back(
      new RulesModel_::Rule_::RuleActionSpec_::CapturingGroupT);
  RulesModel_::Rule_::RuleActionSpec_::CapturingGroupT* greeting_group =
      rule->actions.back()->capturing_group.back().get();
  greeting_group->group_id = 0;
  greeting_group->entity_field.reset(new FlatbufferFieldPathT);
  greeting_group->entity_field->field.emplace_back(new FlatbufferFieldT);
  greeting_group->entity_field->field.back()->field_name = "greeting";
  rule->actions.back()->capturing_group.emplace_back(
      new RulesModel_::Rule_::RuleActionSpec_::CapturingGroupT);
  RulesModel_::Rule_::RuleActionSpec_::CapturingGroupT* location_group =
      rule->actions.back()->capturing_group.back().get();
  location_group->group_id = 1;
  location_group->entity_field.reset(new FlatbufferFieldPathT);
  location_group->entity_field->field.emplace_back(new FlatbufferFieldT);
  location_group->entity_field->field.back()->field_name = "location";

  // Create fake entity data schema meta data.
  // Cannot use object oriented API here as that is not available for the
  // reflection schema.
  flatbuffers::FlatBufferBuilder schema_builder;
  std::vector<flatbuffers::Offset<reflection::Field>> fields = {
      reflection::CreateField(
          schema_builder,
          /*name=*/schema_builder.CreateString("greeting"),
          /*type=*/
          reflection::CreateType(schema_builder,
                                 /*base_type=*/reflection::String),
          /*id=*/0,
          /*offset=*/4),
      reflection::CreateField(
          schema_builder,
          /*name=*/schema_builder.CreateString("location"),
          /*type=*/
          reflection::CreateType(schema_builder,
                                 /*base_type=*/reflection::String),
          /*id=*/1,
          /*offset=*/6),
      reflection::CreateField(
          schema_builder,
          /*name=*/schema_builder.CreateString("person"),
          /*type=*/
          reflection::CreateType(schema_builder,
                                 /*base_type=*/reflection::String),
          /*id=*/2,
          /*offset=*/8)};
  std::vector<flatbuffers::Offset<reflection::Enum>> enums;
  std::vector<flatbuffers::Offset<reflection::Object>> objects = {
      reflection::CreateObject(
          schema_builder,
          /*name=*/schema_builder.CreateString("EntityData"),
          /*fields=*/
          schema_builder.CreateVectorOfSortedTables(&fields))};
  schema_builder.Finish(reflection::CreateSchema(
      schema_builder, schema_builder.CreateVectorOfSortedTables(&objects),
      schema_builder.CreateVectorOfSortedTables(&enums),
      /*(unused) file_ident=*/0,
      /*(unused) file_ext=*/0,
      /*root_table*/ objects[0]));

  actions_model->actions_entity_data_schema.assign(
      schema_builder.GetBufferPointer(),
      schema_builder.GetBufferPointer() + schema_builder.GetSize());

  // Use meta data to generate custom serialized entity data.
  ReflectiveFlatbufferBuilder entity_data_builder(
      flatbuffers::GetRoot<reflection::Schema>(
          actions_model->actions_entity_data_schema.data()));
  std::unique_ptr<ReflectiveFlatbuffer> entity_data =
      entity_data_builder.NewRoot();
  entity_data->Set("person", "Kenobi");
  action->serialized_entity_data = entity_data->Serialize();

  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  std::unique_ptr<ActionsSuggestions> actions_suggestions =
      ActionsSuggestions::FromUnownedBuffer(
          reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
          builder.GetSize(), &unilib_);

  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "hello there", /*time_diff_secs=*/0,
             /*annotations=*/{}, /*locales=*/"en"}}});
  EXPECT_GE(response.actions.size(), 1);
  EXPECT_EQ(response.actions[0].response_text, "General Kenobi!");

  // Check entity data.
  const flatbuffers::Table* entity =
      flatbuffers::GetAnyRoot(reinterpret_cast<const unsigned char*>(
          response.actions[0].serialized_entity_data.data()));
  EXPECT_EQ(entity->GetPointer<const flatbuffers::String*>(/*field=*/4)->str(),
            "hello there");
  EXPECT_EQ(entity->GetPointer<const flatbuffers::String*>(/*field=*/6)->str(),
            "there");
  EXPECT_EQ(entity->GetPointer<const flatbuffers::String*>(/*field=*/8)->str(),
            "Kenobi");
}

TEST_F(ActionsSuggestionsTest, DeduplicateActions) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  ActionsSuggestionsResponse response = actions_suggestions->SuggestActions(
      {{{/*user_id=*/1, "Where are you?", /*time_diff_secs=*/0,
         /*annotations=*/{}, /*locales=*/"en"}}});
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
      new RulesModel_::Rule_::RuleActionSpecT);
  actions_model->rules->rule.back()->actions.back()->action.reset(
      new ActionSuggestionSpecT);
  ActionSuggestionSpecT* action =
      actions_model->rules->rule.back()->actions.back()->action.get();
  action->score = 1.f;
  action->type = ActionsSuggestions::kShareLocation;

  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, actions_model.get()));
  actions_suggestions = ActionsSuggestions::FromUnownedBuffer(
      reinterpret_cast<const uint8_t*>(builder.GetBufferPointer()),
      builder.GetSize(), &unilib_);

  response = actions_suggestions->SuggestActions(
      {{{/*user_id=*/1, "Where are you?", /*time_diff_secs=*/0,
         /*annotations=*/{}, /*locales=*/"en"}}});
  EXPECT_EQ(response.actions.size(), 4);
}
#endif

TEST_F(ActionsSuggestionsTest, SuggestActionsRanking) {
  std::unique_ptr<ActionsSuggestions> actions_suggestions = LoadTestModel();
  std::vector<AnnotatedSpan> annotations(2);
  annotations[0].span = {11, 15};
  annotations[0].classification = {ClassificationResult("address", 1.0)};
  annotations[1].span = {19, 23};
  annotations[1].classification = {ClassificationResult("address", 2.0)};
  const ActionsSuggestionsResponse& response =
      actions_suggestions->SuggestActions(
          {{{/*user_id=*/1, "are you at home or work?",
             /*time_diff_secs=*/0,
             /*annotations=*/annotations,
             /*locales=*/"en"}}});
  EXPECT_GE(response.actions.size(), 2);
  EXPECT_EQ(response.actions[0].type, "view_map");
  EXPECT_EQ(response.actions[0].score, 2.0);
  EXPECT_EQ(response.actions[1].type, "view_map");
  EXPECT_EQ(response.actions[1].score, 1.0);
}

TEST_F(ActionsSuggestionsTest, VisitActionsModel) {
  EXPECT_TRUE(VisitActionsModel<bool>(GetModelPath() + kModelFileName,
                                      [](const ActionsModel* model) {
                                        if (model == nullptr) {
                                          return false;
                                        }
                                        return true;
                                      }));
  EXPECT_FALSE(VisitActionsModel<bool>(GetModelPath() + "non_existing_model.fb",
                                       [](const ActionsModel* model) {
                                         if (model == nullptr) {
                                           return false;
                                         }
                                         return true;
                                       }));
}

}  // namespace
}  // namespace libtextclassifier3
