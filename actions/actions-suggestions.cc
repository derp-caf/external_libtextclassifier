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
  const ActionsModel* model = GetActionsModel(addr);
  flatbuffers::Verifier verifier(addr, size);
  if (model->Verify(verifier)) {
    return model;
  } else {
    return nullptr;
  }
}

// Indices of the TensorFlow Lite model inputs.
enum SmartReplyModelInputs {
  SMART_REPLY_MODEL_INPUT_USER_ID = 0,
  SMART_REPLY_MODEL_INPUT_CONTEXT = 1,
  SMART_REPLY_MODEL_INPUT_CONTEXT_LENGTH = 2,
  SMART_REPLY_MODEL_INPUT_TIME_DIFFS = 3,
  SMART_REPLY_MODEL_INPUT_NUM_SUGGESTIONS = 4
};

// Indices of the TensorFlow Lite model outputs.
enum SmartReplyModelOutputs {
  SMART_REPLY_MODEL_OUTPUT_REPLIES = 0,
  SMART_REPLY_MODEL_OUTPUT_SCORES = 1,
  SMART_REPLY_MODEL_OUTPUT_EMBEDDINGS = 2,
  SMART_REPLY_MODEL_OUTPUT_SENSITIVE_TOPIC_SCORE = 3,
  SMART_REPLY_MODEL_OUTPUT_TRIGGERING_SCORE = 4,
};

// Indices of the TensorFlow Lite actions suggestion model inputs.
enum ActionsSuggestionsModelInputs {
  ACTIONS_SUGGESTIONS_MODEL_INPUT_EMBEDDINGS = 0,
};

// Indices of the TensorFlow Lite actions suggestion model outputss.
enum ActionsSuggestionsModelOutputs {
  ACTIONS_SUGGESTIONS_MODEL_OUTPUT_SCORES = 0,
};

const char* kOtherCategory = "other";

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

bool ActionsSuggestions::ValidateAndInitialize() {
  if (model_ == nullptr) {
    TC3_LOG(ERROR) << "No model specified.";
    return false;
  }

  smart_reply_executor_ = TfLiteModelExecutor::FromBuffer(
      model_->smart_reply_model()->tflite_model());
  if (!smart_reply_executor_) {
    TC3_LOG(ERROR) << "Could not initialize smart reply model executor.";
    return false;
  }

  actions_suggestions_executor_ = TfLiteModelExecutor::FromBuffer(
      model_->actions_suggestions_model()->tflite_model());
  if (!actions_suggestions_executor_) {
    TC3_LOG(ERROR)
        << "Could not initialize actions suggestions model executor.";
    return false;
  }

  return true;
}

void ActionsSuggestions::SetupSmartReplyModelInput(
    const std::vector<std::string>& context, const std::vector<int>& user_ids,
    const int num_suggestions, tflite::Interpreter* interpreter) {
  smart_reply_executor_->SetInput<std::string>(SMART_REPLY_MODEL_INPUT_CONTEXT,
                                               context, interpreter);
  *interpreter
       ->tensor(interpreter->inputs()[SMART_REPLY_MODEL_INPUT_CONTEXT_LENGTH])
       ->data.i64 = context.size();

  smart_reply_executor_->SetInput<int>(SMART_REPLY_MODEL_INPUT_USER_ID,
                                       user_ids, interpreter);

  *interpreter
       ->tensor(interpreter->inputs()[SMART_REPLY_MODEL_INPUT_NUM_SUGGESTIONS])
       ->data.i64 = num_suggestions;
}

void ActionsSuggestions::ReadSmartReplyModelOutput(
    tflite::Interpreter* interpreter,
    std::vector<ActionSuggestion>* suggestions) {
  const std::vector<tflite::StringRef> replies =
      smart_reply_executor_->Output<tflite::StringRef>(
          SMART_REPLY_MODEL_OUTPUT_REPLIES, interpreter);
  TensorView<float> scores = smart_reply_executor_->OutputView<float>(
      SMART_REPLY_MODEL_OUTPUT_SCORES, interpreter);
  std::vector<ActionSuggestion> text_replies;
  for (int i = 0; i < replies.size(); i++) {
    suggestions->push_back({std::string(replies[i].str, replies[i].len),
                            model_->smart_reply_model()->action_type()->str(),
                            scores.data()[i]});
  }
}

void ActionsSuggestions::SuggestActionsFromConversationEmbedding(
    const TensorView<float>& conversation_embedding,
    const ActionSuggestionOptions& options,
    std::vector<ActionSuggestion>* actions) {
  std::unique_ptr<tflite::Interpreter> actions_suggestions_interpreter =
      actions_suggestions_executor_->CreateInterpreter();
  if (!actions_suggestions_interpreter) {
    TC3_LOG(ERROR) << "Could not build TensorFlow Lite interpreter for the "
                      "action suggestions model.";
    return;
  }

  const int embedding_size = conversation_embedding.shape().back();
  actions_suggestions_interpreter->ResizeInputTensor(
      ACTIONS_SUGGESTIONS_MODEL_INPUT_EMBEDDINGS, {1, embedding_size});
  if (actions_suggestions_interpreter->AllocateTensors() != kTfLiteOk) {
    TC3_LOG(ERROR) << "Failed to allocate TensorFlow Lite tensors for the "
                      "action suggestions model.";
    return;
  }

  actions_suggestions_executor_->SetInput(
      ACTIONS_SUGGESTIONS_MODEL_INPUT_EMBEDDINGS, conversation_embedding,
      actions_suggestions_interpreter.get());

  if (actions_suggestions_interpreter->Invoke() != kTfLiteOk) {
    TC3_LOG(ERROR) << "Failed to invoke TensorFlow Lite interpreter.";
    return;
  }

  const TensorView<float> output =
      actions_suggestions_executor_->OutputView<float>(
          ACTIONS_SUGGESTIONS_MODEL_OUTPUT_SCORES,
          actions_suggestions_interpreter.get());
  for (int i = 0; i < model_->actions_suggestions_model()->classes()->Length();
       i++) {
    const std::string& output_class =
        (*model_->actions_suggestions_model()->classes())[i]->str();
    if (output_class == kOtherCategory) {
      continue;
    }
    const float score = output.data()[i];
    if (score >= model_->actions_suggestions_model()->min_confidence()) {
      actions->push_back({/*response_text=*/"", output_class, score});
    }
  }
}

std::vector<ActionSuggestion> ActionsSuggestions::SuggestActions(
    const Conversation& conversation, const ActionSuggestionOptions& options) {
  std::vector<ActionSuggestion> suggestions;
  if (conversation.messages.empty()) {
    return suggestions;
  }

  std::unique_ptr<tflite::Interpreter> smart_reply_interpreter =
      smart_reply_executor_->CreateInterpreter();

  if (!smart_reply_interpreter) {
    TC3_LOG(ERROR) << "Could not build TensorFlow Lite interpreter for the "
                      "smart reply model.";
    return suggestions;
  }

  if (smart_reply_interpreter->AllocateTensors() != kTfLiteOk) {
    TC3_LOG(ERROR)
        << "Failed to allocate TensorFlow Lite tensors for the smart "
           "reply model.";
    return suggestions;
  }

  // Use only last message for now.
  SetupSmartReplyModelInput({conversation.messages.back().text},
                            {conversation.messages.back().user_id},
                            /*num_suggestions=*/3,
                            smart_reply_interpreter.get());

  if (smart_reply_interpreter->Invoke() != kTfLiteOk) {
    TC3_LOG(ERROR) << "Failed to invoke TensorFlow Lite interpreter.";
    return suggestions;
  }

  ReadSmartReplyModelOutput(smart_reply_interpreter.get(), &suggestions);

  // Add action predictions.
  const TensorView<float> conversation_embedding =
      smart_reply_executor_->OutputView<float>(
          SMART_REPLY_MODEL_OUTPUT_EMBEDDINGS, smart_reply_interpreter.get());
  SuggestActionsFromConversationEmbedding(conversation_embedding, options,
                                          &suggestions);

  return suggestions;
}

}  // namespace libtextclassifier3
