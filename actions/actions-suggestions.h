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

#ifndef LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_SUGGESTIONS_H_
#define LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_SUGGESTIONS_H_

#include <memory>
#include <string>
#include <vector>

#include "actions/actions_model_generated.h"
#include "annotator/types.h"
#include "utils/memory/mmap.h"
#include "utils/tflite-model-executor.h"

namespace libtextclassifier3 {

// Action suggestion that contains a response text and the type of the response.
struct ActionSuggestion {
  // Text of the action suggestion.
  std::string response_text;
  // Type of the action suggestion.
  std::string type;
  // Score.
  float score;
};

// Represents a single message in the conversation.
struct ConversationMessage {
  // User ID distinguishing the user from other users in the conversation.
  int user_id;
  // Text of the message.
  std::string text;
  // Annotations on the text.
  std::vector<AnnotatedSpan> annotations;
};

// Conversation between multiple users.
struct Conversation {
  // Sequence of messages that were exchanged in the conversation.
  std::vector<ConversationMessage> messages;
};

// Options for suggesting actions.
struct ActionSuggestionOptions {};

// Class for predicting actions following a conversation.
class ActionsSuggestions {
 public:
  static std::unique_ptr<ActionsSuggestions> FromUnownedBuffer(
      const uint8_t* buffer, const int size);
  // Takes ownership of the mmap.
  static std::unique_ptr<ActionsSuggestions> FromScopedMmap(
      std::unique_ptr<libtextclassifier3::ScopedMmap> mmap);
  static std::unique_ptr<ActionsSuggestions> FromFileDescriptor(
      const int fd, const int offset, const int size);
  static std::unique_ptr<ActionsSuggestions> FromFileDescriptor(const int fd);
  static std::unique_ptr<ActionsSuggestions> FromPath(const std::string& path);

  std::vector<ActionSuggestion> SuggestActions(
      const Conversation& conversation,
      const ActionSuggestionOptions& options = ActionSuggestionOptions()) const;

 private:
  // Checks that model contains all required fields, and initializes internal
  // datastructures.
  bool ValidateAndInitialize();

  void SetupModelInput(const std::vector<std::string>& context,
                       const std::vector<int>& user_ids,
                       const int num_suggestions,
                       tflite::Interpreter* interpreter) const;
  void ReadModelOutput(tflite::Interpreter* interpreter,
                       std::vector<ActionSuggestion>* suggestions) const;

  void SuggestActionsFromModel(
      const Conversation& conversation,
      std::vector<ActionSuggestion>* suggestions) const;

  void SuggestActionsFromAnnotations(
      const Conversation& conversation,
      std::vector<ActionSuggestion>* suggestions) const;

  // Check whether we shouldn't produce any predictions.
  bool ShouldSuppressPredictions(tflite::Interpreter* interpreter) const;

  const ActionsModel* model_;
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap_;

  // Tensorflow Lite models.
  std::unique_ptr<const TfLiteModelExecutor> model_executor_;
};

// Interprets the buffer as a Model flatbuffer and returns it for reading.
const ActionsModel* ViewActionsModel(const void* buffer, int size);

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_SUGGESTIONS_H_
