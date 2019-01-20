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
#include "utils/base/logging.h"
#include "utils/utf8/unicodetext.h"
#include "tensorflow/contrib/lite/string_util.h"

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
const ActionsModel* LoadAndVerifyModel(const uint8_t* addr, int size) {
  flatbuffers::Verifier verifier(addr, size);
  if (VerifyActionsModelBuffer(verifier)) {
    return GetActionsModel(addr);
  } else {
    return nullptr;
  }
}

// Checks whether two annotations can be considered equivalent.
bool IsEquivalentActionAnnotation(const ActionSuggestionAnnotation& annotation,
                                  const ActionSuggestionAnnotation& other) {
  return annotation.message_index == other.message_index &&
         annotation.span == other.span && annotation.name == other.name &&
         annotation.entity.collection == other.entity.collection;
}

// Checks whether two action suggestions can be considered equivalent.
bool IsEquivalentActionSuggestion(const ActionSuggestion& action,
                                  const ActionSuggestion& other) {
  if (action.type != other.type ||
      action.response_text != other.response_text ||
      action.annotations.size() != other.annotations.size()) {
    return false;
  }

  // Check whether annotations are the same.
  for (int i = 0; i < action.annotations.size(); i++) {
    if (!IsEquivalentActionAnnotation(action.annotations[i],
                                      other.annotations[i])) {
      return false;
    }
  }
  return true;
}

// Checks whether any action is equivalent to the given one.
bool IsAnyActionEquivalent(const ActionSuggestion& action,
                           const std::vector<ActionSuggestion>& actions) {
  for (const ActionSuggestion& other : actions) {
    if (IsEquivalentActionSuggestion(action, other)) {
      return true;
    }
  }
  return false;
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

  return true;
}

bool ActionsSuggestions::InitializeRules(ZlibDecompressor* decompressor) {
  if (model_->rules() == nullptr) {
    // No rules specified.
    return true;
  }

  const int num_rules = model_->rules()->rule()->size();
  for (int i = 0; i < num_rules; i++) {
    const auto* rule = model_->rules()->rule()->Get(i);
    std::unique_ptr<UniLib::RegexPattern> compiled_pattern =
        UncompressMakeRegexPattern(*unilib_, rule->pattern(),
                                   rule->compressed_pattern(), decompressor);
    if (compiled_pattern == nullptr) {
      TC3_LOG(ERROR) << "Failed to load rule pattern.";
      return false;
    }
    rules_.push_back({/*rule_id=*/i, std::move(compiled_pattern)});
  }

  return true;
}

void ActionsSuggestions::RankActions(
    ActionsSuggestionsResponse* suggestions) const {
  // First order suggestions by score.
  std::sort(suggestions->actions.begin(), suggestions->actions.end(),
            [](const ActionSuggestion& a, const ActionSuggestion& b) {
              return a.score > b.score;
            });

  // Deduplicate, keeping the higher score actions.
  std::vector<ActionSuggestion> deduplicated_actions;
  for (const ActionSuggestion& candidate : suggestions->actions) {
    // Check whether we already have an equivalent action.
    if (!IsAnyActionEquivalent(candidate, deduplicated_actions)) {
      deduplicated_actions.push_back(candidate);
    }
  }
  suggestions->actions = deduplicated_actions;
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
    tflite::Interpreter* interpreter,
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

  ReadModelOutput(interpreter.get(), response);
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
  for (const AnnotatedSpan& annotation : annotations) {
    if (annotation.classification.empty() ||
        annotation.classification[0].collection.empty()) {
      continue;
    }
    CreateActionsFromAnnotationResult(message_index, annotation, response);
  }
}

void ActionsSuggestions::CreateActionsFromAnnotationResult(
    const int message_index, const AnnotatedSpan& annotation,
    ActionsSuggestionsResponse* suggestions) const {
  const ClassificationResult& classification_result =
      annotation.classification[0];
  ActionSuggestionAnnotation suggestion_annotation;
  suggestion_annotation.message_index = message_index;
  suggestion_annotation.span = annotation.span;
  suggestion_annotation.entity = classification_result;
  const std::string collection = classification_result.collection;

  for (const AnnotationActionsSpec_::AnnotationMapping* mapping :
       *model_->annotation_actions_spec()->annotation_mapping()) {
    if (collection == mapping->annotation_collection()->str()) {
      if (classification_result.score < mapping->min_annotation_score()) {
        continue;
      }
      const float score =
          (mapping->use_annotation_score() ? classification_result.score
                                           : mapping->action()->score());
      suggestions->actions.push_back({/*response_text=*/"",
                                      /*type=*/mapping->action()->type()->str(),
                                      /*score=*/score,
                                      /*annotations=*/{suggestion_annotation}});
    }
  }
}

void ActionsSuggestions::SuggestActionsFromRules(
    const Conversation& conversation,
    ActionsSuggestionsResponse* suggestions) const {
  // Create actions based on rules checking the last message.
  const std::string& message = conversation.messages.back().text;
  const UnicodeText message_unicode(
      UTF8ToUnicodeText(message, /*do_copy=*/false));
  for (int i = 0; i < rules_.size(); i++) {
    const std::unique_ptr<UniLib::RegexMatcher> matcher =
        rules_[i].pattern->Matcher(message_unicode);
    int status = UniLib::RegexMatcher::kNoError;
    if (matcher->Find(&status) && status == UniLib::RegexMatcher::kNoError) {
      const auto actions =
          model_->rules()->rule()->Get(rules_[i].rule_id)->actions();
      for (int k = 0; k < actions->size(); k++) {
        const ActionSuggestionSpec* action = actions->Get(k);
        suggestions->actions.push_back(
            {/*response_text=*/(action->response_text() != nullptr
                                    ? action->response_text()->str()
                                    : ""),
             /*type=*/action->type()->str(),
             /*score=*/action->score()});
      }
    }
  }
}

ActionsSuggestionsResponse ActionsSuggestions::SuggestActions(
    const Conversation& conversation, const Annotator* annotator,
    const ActionSuggestionOptions& options) const {
  ActionsSuggestionsResponse response;
  if (conversation.messages.empty()) {
    return response;
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
    return response;
  }

  int input_text_length = 0;
  for (int i = conversation.messages.size() - num_messages;
       i < conversation.messages.size(); i++) {
    input_text_length += conversation.messages[i].text.length();
  }

  // Bail out if we are provided with too few or too much input.
  if (input_text_length < model_->preconditions()->min_input_length() ||
      (model_->preconditions()->max_input_length() >= 0 &&
       input_text_length > model_->preconditions()->max_input_length())) {
    TC3_LOG(INFO) << "Too much or not enough input for inference.";
    return response;
  }

  SuggestActionsFromRules(conversation, &response);

  SuggestActionsFromModel(conversation, num_messages, &response);

  // Suppress all predictions if the conversation was deemed sensitive.
  if (model_->preconditions()->suppress_on_sensitive_topic() &&
      response.output_filtered_sensitivity) {
    return response;
  }

  SuggestActionsFromAnnotations(conversation, options, annotator, &response);

  RankActions(&response);

  return response;
}

ActionsSuggestionsResponse ActionsSuggestions::SuggestActions(
    const Conversation& conversation,
    const ActionSuggestionOptions& options) const {
  return SuggestActions(conversation, /*annotator=*/nullptr, options);
}

const ActionsModel* ViewActionsModel(const void* buffer, int size) {
  if (buffer == nullptr) {
    return nullptr;
  }

  return LoadAndVerifyModel(reinterpret_cast<const uint8_t*>(buffer), size);
}

}  // namespace libtextclassifier3
