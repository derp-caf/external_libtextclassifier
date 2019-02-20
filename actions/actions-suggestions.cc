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

#include <memory>

#include "actions/types.h"
#include "utils/base/logging.h"
#include "utils/regex-match.h"
#include "utils/strings/split.h"
#include "utils/strings/stringpiece.h"
#include "utils/utf8/unicodetext.h"
#include "tensorflow/lite/string_util.h"

namespace libtextclassifier3 {

const std::string& ActionsSuggestions::kViewCalendarType =
    *[]() { return new std::string("view_calendar"); }();
const std::string& ActionsSuggestions::kViewMapType =
    *[]() { return new std::string("view_map"); }();
const std::string& ActionsSuggestions::kTrackFlightType =
    *[]() { return new std::string("track_flight"); }();
const std::string& ActionsSuggestions::kOpenUrlType =
    *[]() { return new std::string("open_url"); }();
const std::string& ActionsSuggestions::kSendSmsType =
    *[]() { return new std::string("send_sms"); }();
const std::string& ActionsSuggestions::kCallPhoneType =
    *[]() { return new std::string("call_phone"); }();
const std::string& ActionsSuggestions::kSendEmailType =
    *[]() { return new std::string("send_email"); }();
const std::string& ActionsSuggestions::kShareLocation =
    *[]() { return new std::string("share_location"); }();

namespace {
constexpr const char* kAnyMatch = "*";

const ActionsModel* LoadAndVerifyModel(const uint8_t* addr, int size) {
  flatbuffers::Verifier verifier(addr, size);
  if (VerifyActionsModelBuffer(verifier)) {
    return GetActionsModel(addr);
  } else {
    return nullptr;
  }
}

}  // namespace

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromUnownedBuffer(
    const uint8_t* buffer, const int size, const UniLib* unilib) {
  auto actions = std::unique_ptr<ActionsSuggestions>(new ActionsSuggestions());
  const ActionsModel* model = LoadAndVerifyModel(buffer, size);
  if (model == nullptr) {
    return nullptr;
  }
  actions->model_ = model;
  actions->SetOrCreateUnilib(unilib);
  if (!actions->ValidateAndInitialize()) {
    return nullptr;
  }
  return actions;
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromScopedMmap(
    std::unique_ptr<libtextclassifier3::ScopedMmap> mmap,
    const UniLib* unilib) {
  if (!mmap->handle().ok()) {
    TC3_VLOG(1) << "Mmap failed.";
    return nullptr;
  }
  const ActionsModel* model = LoadAndVerifyModel(
      reinterpret_cast<const uint8_t*>(mmap->handle().start()),
      mmap->handle().num_bytes());
  if (!model) {
    TC3_LOG(ERROR) << "Model verification failed.";
    return nullptr;
  }
  auto actions = std::unique_ptr<ActionsSuggestions>(new ActionsSuggestions());
  actions->model_ = model;
  actions->mmap_ = std::move(mmap);
  actions->SetOrCreateUnilib(unilib);
  if (!actions->ValidateAndInitialize()) {
    return nullptr;
  }
  return actions;
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromFileDescriptor(
    const int fd, const int offset, const int size, const UniLib* unilib) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd, offset, size));
  return FromScopedMmap(std::move(mmap), unilib);
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromFileDescriptor(
    const int fd, const UniLib* unilib) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd));
  return FromScopedMmap(std::move(mmap), unilib);
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromPath(
    const std::string& path, const UniLib* unilib) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(path));
  return FromScopedMmap(std::move(mmap), unilib);
}

void ActionsSuggestions::SetOrCreateUnilib(const UniLib* unilib) {
  if (unilib != nullptr) {
    unilib_ = unilib;
  } else {
    owned_unilib_.reset(new UniLib);
    unilib_ = owned_unilib_.get();
  }
}

bool ActionsSuggestions::ValidateAndInitialize() {
  if (model_ == nullptr) {
    TC3_LOG(ERROR) << "No model specified.";
    return false;
  }

  if (model_->preconditions() == nullptr) {
    TC3_LOG(ERROR) << "No triggering conditions specified.";
    return false;
  }

  if (model_->locales() &&
      !ParseLocales(model_->locales()->c_str(), &locales_)) {
    TC3_LOG(ERROR) << "Could not parse model supported locales.";
    return false;
  }

  if (model_->tflite_model_spec()) {
    model_executor_ = TfLiteModelExecutor::FromBuffer(
        model_->tflite_model_spec()->tflite_model());
    if (!model_executor_) {
      TC3_LOG(ERROR) << "Could not initialize model executor.";
      return false;
    }
  }

  std::unique_ptr<ZlibDecompressor> decompressor = ZlibDecompressor::Instance();
  if (!InitializeRules(decompressor.get())) {
    TC3_LOG(ERROR) << "Could not initialize rules.";
    return false;
  }

  if (model_->actions_entity_data_schema()) {
    entity_data_schema_ = LoadAndVerifyFlatbuffer<reflection::Schema>(
        model_->actions_entity_data_schema()->Data(),
        model_->actions_entity_data_schema()->size());
    if (entity_data_schema_ == nullptr) {
      TC3_LOG(ERROR) << "Could not load entity data schema data.";
      return false;
    }

    entity_data_builder_.reset(
        new ReflectiveFlatbufferBuilder(entity_data_schema_));
  } else {
    entity_data_schema_ = nullptr;
  }

  if (!(ranker_ = ActionsSuggestionsRanker::CreateActionsSuggestionsRanker(
            model_->ranking_options()))) {
    TC3_LOG(ERROR) << "Could not create an action suggestions ranker.";
    return false;
  }

  return true;
}

bool ActionsSuggestions::InitializeRules(ZlibDecompressor* decompressor) {
  if (model_->rules() != nullptr) {
    if (!InitializeRules(decompressor, model_->rules(), &rules_)) {
      TC3_LOG(ERROR) << "Could not initialize action rules.";
      return false;
    }
  }

  if (model_->preconditions()->suppress_on_low_confidence_input() &&
      model_->preconditions()->low_confidence_rules() != nullptr) {
    if (!InitializeRules(decompressor,
                         model_->preconditions()->low_confidence_rules(),
                         &low_confidence_rules_)) {
      TC3_LOG(ERROR) << "Could not initialize low confidence rules.";
      return false;
    }
  }

  return true;
}

bool ActionsSuggestions::InitializeRules(
    ZlibDecompressor* decompressor, const RulesModel* rules,
    std::vector<CompiledRule>* compiled_rules) const {
  for (const RulesModel_::Rule* rule : *rules->rule()) {
    std::unique_ptr<UniLib::RegexPattern> compiled_pattern =
        UncompressMakeRegexPattern(*unilib_, rule->pattern(),
                                   rule->compressed_pattern(), decompressor);
    if (compiled_pattern == nullptr) {
      TC3_LOG(ERROR) << "Failed to load rule pattern.";
      return false;
    }
    compiled_rules->push_back({rule, std::move(compiled_pattern)});
  }

  return true;
}

bool ActionsSuggestions::IsLocaleSupportedByModel(const Locale& locale) const {
  if (!locale.IsValid()) {
    return false;
  }
  if (locale.IsUnknown()) {
    return model_->preconditions()->handle_unknown_locale_as_supported();
  }
  for (const Locale& model_locale : locales_) {
    if (!model_locale.IsValid()) {
      continue;
    }
    const bool language_matches = model_locale.Language().empty() ||
                                  model_locale.Language() == kAnyMatch ||
                                  model_locale.Language() == locale.Language();
    const bool script_matches =
        model_locale.Script().empty() || model_locale.Script() == kAnyMatch ||
        locale.Script().empty() || model_locale.Script() == locale.Script();
    const bool region_matches =
        model_locale.Region().empty() || model_locale.Region() == kAnyMatch ||
        locale.Region().empty() || model_locale.Region() == locale.Region();
    if (language_matches && script_matches && region_matches) {
      return true;
    }
  }
  return false;
}

bool ActionsSuggestions::IsAnyLocaleSupportedByModel(
    const std::vector<Locale>& locales) const {
  if (locales.empty()) {
    return model_->preconditions()->handle_missing_locale_as_supported();
  }
  for (const Locale& locale : locales) {
    if (IsLocaleSupportedByModel(locale)) {
      return true;
    }
  }
  return false;
}

bool ActionsSuggestions::IsLowConfidenceInput(const Conversation& conversation,
                                              const int num_messages) const {
  for (int i = 1; i <= num_messages; i++) {
    const std::string& message =
        conversation.messages[conversation.messages.size() - i].text;
    const UnicodeText message_unicode(
        UTF8ToUnicodeText(message, /*do_copy=*/false));
    for (const CompiledRule& rule : low_confidence_rules_) {
      const std::unique_ptr<UniLib::RegexMatcher> matcher =
          rule.pattern->Matcher(message_unicode);
      int status = UniLib::RegexMatcher::kNoError;
      if (matcher->Find(&status) && status == UniLib::RegexMatcher::kNoError) {
        return true;
      }
    }
  }
  return false;
}

void ActionsSuggestions::SetupModelInput(
    const std::vector<std::string>& context, const std::vector<int>& user_ids,
    const std::vector<float>& time_diffs, const int num_suggestions,
    tflite::Interpreter* interpreter) const {
  if (model_->tflite_model_spec()->input_context() >= 0) {
    model_executor_->SetInput<std::string>(
        model_->tflite_model_spec()->input_context(), context, interpreter);
  }
  if (model_->tflite_model_spec()->input_context_length() >= 0) {
    *interpreter
         ->tensor(interpreter->inputs()[model_->tflite_model_spec()
                                            ->input_context_length()])
         ->data.i64 = context.size();
  }
  if (model_->tflite_model_spec()->input_user_id() >= 0) {
    model_executor_->SetInput<int>(model_->tflite_model_spec()->input_user_id(),
                                   user_ids, interpreter);
  }
  if (model_->tflite_model_spec()->input_num_suggestions() >= 0) {
    *interpreter
         ->tensor(interpreter->inputs()[model_->tflite_model_spec()
                                            ->input_num_suggestions()])
         ->data.i64 = num_suggestions;
  }
  if (model_->tflite_model_spec()->input_time_diffs() >= 0) {
    model_executor_->SetInput<float>(
        model_->tflite_model_spec()->input_time_diffs(), time_diffs,
        interpreter);
  }
}

void ActionsSuggestions::ReadModelOutput(
    tflite::Interpreter* interpreter, const ActionSuggestionOptions& options,
    ActionsSuggestionsResponse* response) const {
  // Read sensitivity and triggering score predictions.
  if (model_->tflite_model_spec()->output_triggering_score() >= 0) {
    const TensorView<float>& triggering_score =
        model_executor_->OutputView<float>(
            model_->tflite_model_spec()->output_triggering_score(),
            interpreter);
    if (!triggering_score.is_valid() || triggering_score.size() == 0) {
      TC3_LOG(ERROR) << "Could not compute triggering score.";
      return;
    }
    response->triggering_score = triggering_score.data()[0];
    response->output_filtered_min_triggering_score =
        !options.ignore_min_replies_triggering_threshold &&
        (response->triggering_score <
         model_->preconditions()->min_smart_reply_triggering_score());
  }
  if (model_->tflite_model_spec()->output_sensitive_topic_score() >= 0) {
    const TensorView<float>& sensitive_topic_score =
        model_executor_->OutputView<float>(
            model_->tflite_model_spec()->output_sensitive_topic_score(),
            interpreter);
    if (!sensitive_topic_score.is_valid() ||
        sensitive_topic_score.dim(0) != 1) {
      TC3_LOG(ERROR) << "Could not compute sensitive topic score.";
      return;
    }
    response->sensitivity_score = sensitive_topic_score.data()[0];
    response->output_filtered_sensitivity =
        (response->sensitivity_score >
         model_->preconditions()->max_sensitive_topic_score());
  }

  // Suppress model outputs.
  if (response->output_filtered_sensitivity) {
    return;
  }

  // Read smart reply predictions.
  if (!response->output_filtered_min_triggering_score &&
      model_->tflite_model_spec()->output_replies() >= 0) {
    const std::vector<tflite::StringRef> replies =
        model_executor_->Output<tflite::StringRef>(
            model_->tflite_model_spec()->output_replies(), interpreter);
    TensorView<float> scores = model_executor_->OutputView<float>(
        model_->tflite_model_spec()->output_replies_scores(), interpreter);
    std::vector<ActionSuggestion> text_replies;
    for (int i = 0; i < replies.size(); i++) {
      if (replies[i].len == 0) continue;
      response->actions.push_back({std::string(replies[i].str, replies[i].len),
                                   model_->smart_reply_action_type()->str(),
                                   scores.data()[i]});
    }
  }

  // Read actions suggestions.
  if (model_->tflite_model_spec()->output_actions_scores() >= 0) {
    const TensorView<float> actions_scores = model_executor_->OutputView<float>(
        model_->tflite_model_spec()->output_actions_scores(), interpreter);
    for (int i = 0; i < model_->action_type()->Length(); i++) {
      // Skip disabled action classes, such as the default other category.
      if (!(*model_->action_type())[i]->enabled()) {
        continue;
      }
      const float score = actions_scores.data()[i];
      if (score < (*model_->action_type())[i]->min_triggering_score()) {
        continue;
      }
      const std::string& output_class =
          (*model_->action_type())[i]->name()->str();
      response->actions.push_back({/*response_text=*/"", output_class, score});
    }
  }
}

void ActionsSuggestions::SuggestActionsFromModel(
    const Conversation& conversation, const int num_messages,
    const ActionSuggestionOptions& options,
    ActionsSuggestionsResponse* response) const {
  TC3_CHECK_LE(num_messages, conversation.messages.size());

  if (!model_executor_) {
    return;
  }
  std::unique_ptr<tflite::Interpreter> interpreter =
      model_executor_->CreateInterpreter();

  if (!interpreter) {
    TC3_LOG(ERROR) << "Could not build TensorFlow Lite interpreter for the "
                      "actions suggestions model.";
    return;
  }

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    TC3_LOG(ERROR)
        << "Failed to allocate TensorFlow Lite tensors for the actions "
           "suggestions model.";
    return;
  }

  std::vector<std::string> context;
  std::vector<int> user_ids;
  std::vector<float> time_diffs;

  // Gather last `num_messages` messages from the conversation.
  int64 last_message_reference_time_ms_utc = 0;
  const float second_in_ms = 1000;
  for (int i = conversation.messages.size() - num_messages;
       i < conversation.messages.size(); i++) {
    const ConversationMessage& message = conversation.messages[i];
    context.push_back(message.text);
    user_ids.push_back(message.user_id);

    float time_diff_secs = 0;
    if (message.reference_time_ms_utc != 0 &&
        last_message_reference_time_ms_utc != 0) {
      time_diff_secs = std::max(0.0f, (message.reference_time_ms_utc -
                                       last_message_reference_time_ms_utc) /
                                          second_in_ms);
    }
    if (message.reference_time_ms_utc != 0) {
      last_message_reference_time_ms_utc = message.reference_time_ms_utc;
    }
    time_diffs.push_back(time_diff_secs);
  }

  SetupModelInput(context, user_ids, time_diffs,
                  /*num_suggestions=*/model_->num_smart_replies(),
                  interpreter.get());

  if (interpreter->Invoke() != kTfLiteOk) {
    TC3_LOG(ERROR) << "Failed to invoke TensorFlow Lite interpreter.";
    return;
  }

  ReadModelOutput(interpreter.get(), options, response);
}

void ActionsSuggestions::SuggestActionsFromAnnotations(
    const Conversation& conversation, const ActionSuggestionOptions& options,
    const Annotator* annotator, ActionsSuggestionsResponse* response) const {
  if (model_->annotation_actions_spec() == nullptr ||
      model_->annotation_actions_spec()->annotation_mapping() == nullptr ||
      model_->annotation_actions_spec()->annotation_mapping()->size() == 0) {
    return;
  }

  // Create actions based on the annotations present in the last message.
  std::vector<AnnotatedSpan> annotations =
      conversation.messages.back().annotations;
  if (annotations.empty() && annotator != nullptr) {
    annotations = annotator->Annotate(conversation.messages.back().text,
                                      options.annotation_options);
  }
  const int message_index = conversation.messages.size() - 1;
  std::vector<ActionSuggestionAnnotation> action_annotations;
  action_annotations.reserve(annotations.size());
  for (const AnnotatedSpan& annotation : annotations) {
    if (annotation.classification.empty()) {
      continue;
    }

    const ClassificationResult& classification_result =
        annotation.classification[0];

    ActionSuggestionAnnotation action_annotation;
    action_annotation.message_index = message_index;
    action_annotation.span = annotation.span;
    action_annotation.entity = classification_result;
    action_annotation.name = classification_result.collection;
    action_annotation.text =
        UTF8ToUnicodeText(conversation.messages.back().text,
                          /*do_copy=*/false)
            .UTF8Substring(annotation.span.first, annotation.span.second);
    action_annotations.push_back(action_annotation);
  }

  if (model_->annotation_actions_spec()->deduplicate_annotations()) {
    // Create actions only for deduplicated annotations.
    for (const int annotation_id : DeduplicateAnnotations(action_annotations)) {
      CreateActionsFromAnnotation(message_index,
                                  action_annotations[annotation_id], response);
    }
  } else {
    // Create actions for all annotations.
    for (const ActionSuggestionAnnotation& annotation : action_annotations) {
      CreateActionsFromAnnotation(message_index, annotation, response);
    }
  }
}

std::vector<int> ActionsSuggestions::DeduplicateAnnotations(
    const std::vector<ActionSuggestionAnnotation>& annotations) const {
  std::map<std::pair<std::string, std::string>, int> deduplicated_annotations;

  for (int i = 0; i < annotations.size(); i++) {
    const std::pair<std::string, std::string> key = {annotations[i].name,
                                                     annotations[i].text};
    auto entry = deduplicated_annotations.find(key);
    if (entry != deduplicated_annotations.end()) {
      // Kepp the annotation with the higher score.
      if (annotations[entry->second].entity.score <
          annotations[i].entity.score) {
        entry->second = i;
      }
      continue;
    }
    deduplicated_annotations.insert(entry, {key, i});
  }

  std::vector<int> result;
  result.reserve(deduplicated_annotations.size());
  for (const auto& key_and_annotation : deduplicated_annotations) {
    result.push_back(key_and_annotation.second);
  }
  return result;
}

void ActionsSuggestions::CreateActionsFromAnnotation(
    const int message_index, const ActionSuggestionAnnotation& annotation,
    ActionsSuggestionsResponse* suggestions) const {
  for (const AnnotationActionsSpec_::AnnotationMapping* mapping :
       *model_->annotation_actions_spec()->annotation_mapping()) {
    if (annotation.entity.collection ==
        mapping->annotation_collection()->str()) {
      if (annotation.entity.score < mapping->min_annotation_score()) {
        continue;
      }
      const float score =
          (mapping->use_annotation_score() ? annotation.entity.score
                                           : mapping->action()->score());

      std::string serialized_entity_data;
      if (mapping->action()->serialized_entity_data()) {
        serialized_entity_data =
            mapping->action()->serialized_entity_data()->str();
      }

      suggestions->actions.push_back({
          /*response_text=*/"",
          /*type=*/mapping->action()->type()->str(),
          /*score=*/score,
          /*annotations=*/{annotation},
          /*serialized_entity_data=*/serialized_entity_data,
      });
    }
  }
}

bool ActionsSuggestions::HasEntityData(const RulesModel_::Rule* rule) const {
  for (const RulesModel_::Rule_::RuleActionSpec* rule_action :
       *rule->actions()) {
    if (rule_action->action()->serialized_entity_data() != nullptr ||
        rule_action->capturing_group() != nullptr) {
      return true;
    }
  }
  return false;
}

bool ActionsSuggestions::SuggestActionsFromRules(
    const Conversation& conversation,
    ActionsSuggestionsResponse* suggestions) const {
  // Create actions based on rules checking the last message.
  const std::string& message = conversation.messages.back().text;
  const UnicodeText message_unicode(
      UTF8ToUnicodeText(message, /*do_copy=*/false));
  for (const CompiledRule& rule : rules_) {
    const std::unique_ptr<UniLib::RegexMatcher> matcher =
        rule.pattern->Matcher(message_unicode);
    int status = UniLib::RegexMatcher::kNoError;
    const bool has_entity_data = HasEntityData(rule.rule);
    while (matcher->Find(&status) && status == UniLib::RegexMatcher::kNoError) {
      for (const RulesModel_::Rule_::RuleActionSpec* rule_action :
           *rule.rule->actions()) {
        const ActionSuggestionSpec* action = rule_action->action();

        std::string serialized_entity_data;
        if (has_entity_data) {
          TC3_CHECK(entity_data_builder_ != nullptr);
          std::unique_ptr<ReflectiveFlatbuffer> entity_data =
              entity_data_builder_->NewRoot();
          TC3_CHECK(entity_data != nullptr);

          // Set static entity data.
          if (action->serialized_entity_data() != nullptr) {
            entity_data->MergeFromSerializedFlatbuffer(
                StringPiece(action->serialized_entity_data()->c_str(),
                            action->serialized_entity_data()->size()));
          }

          // Add entity data from rule capturing groups.
          if (rule_action->capturing_group() != nullptr) {
            for (const RulesModel_::Rule_::RuleActionSpec_::CapturingGroup*
                     group : *rule_action->capturing_group()) {
              if (!SetFieldFromCapturingGroup(
                      group->group_id(), group->entity_field(), matcher.get(),
                      entity_data.get())) {
                TC3_LOG(ERROR)
                    << "Could not set entity data from rule capturing group.";
                return false;
              }
            }
          }

          serialized_entity_data = entity_data->Serialize();
        }
        suggestions->actions.push_back(
            {/*response_text=*/(action->response_text() != nullptr
                                    ? action->response_text()->str()
                                    : ""),
             /*type=*/action->type()->str(),
             /*score=*/action->score(),
             /*annotations=*/{},
             /*serialized_entity_data=*/serialized_entity_data});
      }
    }
  }
  return true;
}

bool ActionsSuggestions::GatherActionsSuggestions(
    const Conversation& conversation, const Annotator* annotator,
    const ActionSuggestionOptions& options,
    ActionsSuggestionsResponse* response) const {
  if (conversation.messages.empty()) {
    return true;
  }

  const int conversation_history_length = conversation.messages.size();
  const int max_conversation_history_length =
      model_->max_conversation_history_length();
  const int num_messages =
      ((max_conversation_history_length < 0 ||
        conversation_history_length < max_conversation_history_length)
           ? conversation_history_length
           : max_conversation_history_length);

  if (num_messages <= 0) {
    TC3_LOG(INFO) << "No messages provided for actions suggestions.";
    return false;
  }

  SuggestActionsFromAnnotations(conversation, options, annotator, response);

  int input_text_length = 0;
  int num_matching_locales = 0;
  for (int i = conversation.messages.size() - num_messages;
       i < conversation.messages.size(); i++) {
    input_text_length += conversation.messages[i].text.length();
    std::vector<Locale> message_locales;
    if (!ParseLocales(conversation.messages[i].locales, &message_locales)) {
      continue;
    }
    if (IsAnyLocaleSupportedByModel(message_locales)) {
      ++num_matching_locales;
    }
  }

  // Bail out if we are provided with too few or too much input.
  if (input_text_length < model_->preconditions()->min_input_length() ||
      (model_->preconditions()->max_input_length() >= 0 &&
       input_text_length > model_->preconditions()->max_input_length())) {
    TC3_LOG(INFO) << "Too much or not enough input for inference.";
    return response;
  }

  // Bail out if the text does not look like it can be handled by the model.
  const float matching_fraction =
      static_cast<float>(num_matching_locales) / num_messages;
  if (matching_fraction <
      model_->preconditions()->min_locale_match_fraction()) {
    TC3_LOG(INFO) << "Not enough locale matches.";
    response->output_filtered_locale_mismatch = true;
    return true;
  }

  if (IsLowConfidenceInput(conversation, num_messages)) {
    TC3_LOG(INFO) << "Low confidence input.";
    response->output_filtered_low_confidence = true;
    return true;
  }

  SuggestActionsFromModel(conversation, num_messages, options, response);

  // Suppress all predictions if the conversation was deemed sensitive.
  if (model_->preconditions()->suppress_on_sensitive_topic() &&
      response->output_filtered_sensitivity) {
    return true;
  }

  if (!SuggestActionsFromRules(conversation, response)) {
    TC3_LOG(ERROR) << "Could not suggest actions from rules.";
    return false;
  }

  return true;
}

ActionsSuggestionsResponse ActionsSuggestions::SuggestActions(
    const Conversation& conversation, const Annotator* annotator,
    const ActionSuggestionOptions& options) const {
  ActionsSuggestionsResponse response;
  if (!GatherActionsSuggestions(conversation, annotator, options, &response)) {
    TC3_LOG(ERROR) << "Could not gather actions suggestions.";
    response.actions.clear();
  } else if (!ranker_->RankActions(&response)) {
    TC3_LOG(ERROR) << "Could not rank actions.";
    response.actions.clear();
  }
  return response;
}

ActionsSuggestionsResponse ActionsSuggestions::SuggestActions(
    const Conversation& conversation,
    const ActionSuggestionOptions& options) const {
  return SuggestActions(conversation, /*annotator=*/nullptr, options);
}

const ActionsModel* ActionsSuggestions::model() const { return model_; }
const reflection::Schema* ActionsSuggestions::entity_data_schema() const {
  return entity_data_schema_;
}

const ActionsModel* ViewActionsModel(const void* buffer, int size) {
  if (buffer == nullptr) {
    return nullptr;
  }
  return LoadAndVerifyModel(reinterpret_cast<const uint8_t*>(buffer), size);
}

}  // namespace libtextclassifier3
