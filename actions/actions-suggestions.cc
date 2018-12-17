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
#include "tensorflow/contrib/lite/string_util.h"

namespace libtextclassifier3 {

namespace {
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
    const uint8_t* buffer, const int size) {
  auto actions = std::unique_ptr<ActionsSuggestions>(new ActionsSuggestions());
  const ActionsModel* model = LoadAndVerifyModel(buffer, size);
  if (model == nullptr) {
    return nullptr;
  }
  actions->model_ = model;
  if (!actions->ValidateAndInitialize()) {
    return nullptr;
  }
  return actions;
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromScopedMmap(
    std::unique_ptr<libtextclassifier3::ScopedMmap> mmap) {
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

  if (!actions->ValidateAndInitialize()) {
    return nullptr;
  }
  return actions;
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromFileDescriptor(
    const int fd, const int offset, const int size) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd, offset, size));
  return FromScopedMmap(std::move(mmap));
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromFileDescriptor(
    const int fd) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd));
  return FromScopedMmap(std::move(mmap));
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromPath(
    const std::string& path) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(path));
  return FromScopedMmap(std::move(mmap));
}

void ActionsSuggestions::SetAnnotator(const Annotator* annotator) {
  annotator_ = annotator;
}

bool ActionsSuggestions::ValidateAndInitialize() {
  if (model_ == nullptr) {
    TC3_LOG(ERROR) << "No model specified.";
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

  return true;
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

bool ActionsSuggestions::ShouldSuppressPredictions(
    tflite::Interpreter* interpreter) const {
  const TensorView<float>& triggering_score =
      model_executor_->OutputView<float>(
          model_->tflite_model_spec()->output_triggering_score(), interpreter);
  if (!triggering_score.is_valid() || triggering_score.dim(0) != 1) {
    TC3_LOG(ERROR) << "Could not compute triggering score.";
    return true;
  }
  if (triggering_score.data()[0] <= model_->min_triggering_confidence()) {
    return true;
  }

  const TensorView<float>& sensitive_topic_score =
      model_executor_->OutputView<float>(
          model_->tflite_model_spec()->output_sensitive_topic_score(),
          interpreter);
  if (!sensitive_topic_score.is_valid() || sensitive_topic_score.dim(0) != 1) {
    TC3_LOG(ERROR) << "Could not compute sensitive topic score.";
    return true;
  }
  if (sensitive_topic_score.data()[0] > model_->max_sensitive_topic_score()) {
    return true;
  }
  return false;
}

void ActionsSuggestions::ReadModelOutput(
    tflite::Interpreter* interpreter,
    std::vector<ActionSuggestion>* suggestions) const {
  // Read smart reply predictions.
  const std::vector<tflite::StringRef> replies =
      model_executor_->Output<tflite::StringRef>(
          model_->tflite_model_spec()->output_replies(), interpreter);
  TensorView<float> scores = model_executor_->OutputView<float>(
      model_->tflite_model_spec()->output_replies_scores(), interpreter);
  std::vector<ActionSuggestion> text_replies;
  for (int i = 0; i < replies.size(); i++) {
    suggestions->push_back({std::string(replies[i].str, replies[i].len),
                            model_->smart_reply_action_type()->str(),
                            scores.data()[i]});
  }

  // Read actions suggestions.
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
    if (score >= model_->min_actions_confidence()) {
      suggestions->push_back({/*response_text=*/"", output_class, score});
    }
  }
}

void ActionsSuggestions::SuggestActionsFromModel(
    const Conversation& conversation,
    std::vector<ActionSuggestion>* suggestions) const {
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

  int num_messages = conversation.messages.size();
  if (model_->max_conversation_history_length() >= 0 &&
      num_messages > model_->max_conversation_history_length()) {
    num_messages = model_->max_conversation_history_length();
  }

  if (num_messages <= 0) {
    TC3_LOG(INFO) << "No messages provided for actions suggestions.";
    return;
  }

  std::vector<std::string> context;
  std::vector<int> user_ids;
  std::vector<float> time_diffs;

  // Gather last `num__messages` messages from the conversation.
  for (int i = conversation.messages.size() - num_messages;
       i < conversation.messages.size(); i++) {
    const ConversationMessage& message = conversation.messages[i];
    context.push_back(message.text);
    user_ids.push_back(message.user_id);
    time_diffs.push_back(message.time_diff_secs);
  }

  SetupModelInput(context, user_ids, time_diffs,
                  /*num_suggestions=*/model_->num_smart_replies(),
                  interpreter.get());

  if (interpreter->Invoke() != kTfLiteOk) {
    TC3_LOG(ERROR) << "Failed to invoke TensorFlow Lite interpreter.";
    return;
  }

  if (ShouldSuppressPredictions(interpreter.get())) {
    return;
  }

  ReadModelOutput(interpreter.get(), suggestions);
}

void ActionsSuggestions::SuggestActionsFromAnnotations(
    const Conversation& conversation, const ActionSuggestionOptions& options,
    std::vector<ActionSuggestion>* suggestions) const {
  // Create actions based on the annotations present in the last message.
  // TODO(smillius): Make this configurable.
  std::vector<AnnotatedSpan> annotations =
      conversation.messages.back().annotations;
  if (annotations.empty() && annotator_ != nullptr) {
    annotations = annotator_->Annotate(conversation.messages.back().text,
                                       options.annotation_options);
  }
  for (const AnnotatedSpan& annotation : annotations) {
    if (annotation.classification.empty() ||
        annotation.classification[0].collection.empty()) {
      continue;
    }
    const ClassificationResult& classification_result =
        annotation.classification[0];
    suggestions->push_back({/*response_text=*/"",
                            /*type=*/classification_result.collection,
                            /*score=*/classification_result.score});
  }
}

std::vector<ActionSuggestion> ActionsSuggestions::SuggestActions(
    const Conversation& conversation,
    const ActionSuggestionOptions& options) const {
  std::vector<ActionSuggestion> suggestions;
  if (conversation.messages.empty()) {
    return suggestions;
  }

  SuggestActionsFromModel(conversation, &suggestions);
  SuggestActionsFromAnnotations(conversation, options, &suggestions);

  // TODO(smillius): Properly rank the actions.

  return suggestions;
}

const ActionsModel* ViewActionsModel(const void* buffer, int size) {
  if (buffer == nullptr) {
    return nullptr;
  }

  return LoadAndVerifyModel(reinterpret_cast<const uint8_t*>(buffer), size);
}

}  // namespace libtextclassifier3
