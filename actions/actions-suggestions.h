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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "actions/actions_model_generated.h"
#include "annotator/annotator.h"
#include "annotator/types.h"
#include "utils/memory/mmap.h"
#include "utils/tflite-model-executor.h"
#include "utils/utf8/unilib.h"
#include "utils/variant.h"
#include "utils/zlib/zlib.h"

namespace libtextclassifier3 {

// An entity associated with an action.
struct ActionSuggestionAnnotation {
  // The referenced message.
  // -1 if not referencing a particular message in the provided input.
  int message_index;

  // The span within the reference message.
  // (-1, -1) if not referencing a particular location.
  CodepointSpan span;
  ClassificationResult entity;

  // Optional annotation name.
  std::string name;

  explicit ActionSuggestionAnnotation()
      : message_index(kInvalidIndex), span({kInvalidIndex, kInvalidIndex}) {}
};

// Action suggestion that contains a response text and the type of the response.
struct ActionSuggestion {
  // Text of the action suggestion.
  std::string response_text;

  // Type of the action suggestion.
  std::string type;

  // Score.
  float score;

  // The associated annotations.
  std::vector<ActionSuggestionAnnotation> annotations;

  // Extras information.
  std::map<std::string, Variant> extra;
};

// Actions suggestions result containing meta-information and the suggested
// actions.
struct ActionsSuggestionsResponse {
  ActionsSuggestionsResponse()
      : sensitivity_score(-1),
        triggering_score(-1),
        output_filtered_sensitivity(false),
        output_filtered_min_triggering_score(false) {}

  // The sensitivity assessment.
  float sensitivity_score;
  float triggering_score;

  // Whether the output was suppressed by the sensitivity threshold.
  bool output_filtered_sensitivity;

  // Whether the output was suppressed by the triggering score threshold.
  bool output_filtered_min_triggering_score;

  // The suggested actions.
  std::vector<ActionSuggestion> actions;
};

// Represents a single message in the conversation.
struct ConversationMessage {
  // User ID distinguishing the user from other users in the conversation.
  int user_id;

  // Text of the message.
  std::string text;

  // Reference time of this message.
  int64 reference_time_ms_utc;

  // Annotations on the text.
  std::vector<AnnotatedSpan> annotations;

  // Comma-separated list of locale specification for the text in the
  // conversation (BCP 47 tags).
  std::string locales;
};

// Conversation between multiple users.
struct Conversation {
  // Sequence of messages that were exchanged in the conversation.
  std::vector<ConversationMessage> messages;
};

// Options for suggesting actions.
struct ActionSuggestionOptions {
  // Options for annotation of the messages.
  AnnotationOptions annotation_options = AnnotationOptions::Default();
  bool ignore_min_replies_triggering_threshold = false;

  static ActionSuggestionOptions Default() { return ActionSuggestionOptions(); }
};

// Class for predicting actions following a conversation.
class ActionsSuggestions {
 public:
  static std::unique_ptr<ActionsSuggestions> FromUnownedBuffer(
      const uint8_t* buffer, const int size, const UniLib* unilib = nullptr);
  // Takes ownership of the mmap.
  static std::unique_ptr<ActionsSuggestions> FromScopedMmap(
      std::unique_ptr<libtextclassifier3::ScopedMmap> mmap,
      const UniLib* unilib = nullptr);
  static std::unique_ptr<ActionsSuggestions> FromFileDescriptor(
      const int fd, const int offset, const int size,
      const UniLib* unilib = nullptr);
  static std::unique_ptr<ActionsSuggestions> FromFileDescriptor(
      const int fd, const UniLib* unilib = nullptr);
  static std::unique_ptr<ActionsSuggestions> FromPath(
      const std::string& path, const UniLib* unilib = nullptr);

  ActionsSuggestionsResponse SuggestActions(
      const Conversation& conversation,
      const ActionSuggestionOptions& options =
          ActionSuggestionOptions::Default()) const;

  ActionsSuggestionsResponse SuggestActions(
      const Conversation& conversation, const Annotator* annotator,
      const ActionSuggestionOptions& options =
          ActionSuggestionOptions::Default()) const;

  // Provide an annotator.
  void SetAnnotator(const Annotator* annotator);

  float GetMinRepliesTriggeringThreshold() const;

  // Should be in sync with those defined in Android.
  // android/frameworks/base/core/java/android/view/textclassifier/ConversationActions.java
  static const std::string& kViewCalendarType;
  static const std::string& kViewMapType;
  static const std::string& kTrackFlightType;
  static const std::string& kOpenUrlType;
  static const std::string& kSendSmsType;
  static const std::string& kCallPhoneType;
  static const std::string& kSendEmailType;
  static const std::string& kShareLocation;

 private:
  // Checks that model contains all required fields, and initializes internal
  // datastructures.
  bool ValidateAndInitialize();

  void SetOrCreateUnilib(const UniLib* unilib);

  // Initializes regular expression rules.
  bool InitializeRules(ZlibDecompressor* decompressor);

  void SetupModelInput(const std::vector<std::string>& context,
                       const std::vector<int>& user_ids,
                       const std::vector<float>& time_diffs,
                       const int num_suggestions,
                       tflite::Interpreter* interpreter) const;
  void ReadModelOutput(tflite::Interpreter* interpreter,
                       const ActionSuggestionOptions& options,
                       ActionsSuggestionsResponse* response) const;

  void SuggestActionsFromModel(const Conversation& conversation,
                               const int num_messages,
                               const ActionSuggestionOptions& options,
                               ActionsSuggestionsResponse* response) const;

  void SuggestActionsFromAnnotations(
      const Conversation& conversation, const ActionSuggestionOptions& options,
      const Annotator* annotator,
      ActionsSuggestionsResponse* suggestions) const;

  void CreateActionsFromAnnotationResult(
      const int message_index, const AnnotatedSpan& annotation,
      ActionsSuggestionsResponse* suggestions) const;

  void SuggestActionsFromRules(const Conversation& conversation,
                               ActionsSuggestionsResponse* suggestions) const;

  // Rank and deduplicate actions suggestions.
  void RankActions(ActionsSuggestionsResponse* suggestions) const;

  const ActionsModel* model_;
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap_;

  // Tensorflow Lite models.
  std::unique_ptr<const TfLiteModelExecutor> model_executor_;

  // Rules.
  struct CompiledRule {
    const RulesModel_::Rule* rule;
    std::unique_ptr<UniLib::RegexPattern> pattern;
  };
  std::vector<CompiledRule> rules_;

  std::unique_ptr<UniLib> owned_unilib_;
  const UniLib* unilib_;
};

// Interprets the buffer as a Model flatbuffer and returns it for reading.
const ActionsModel* ViewActionsModel(const void* buffer, int size);

// Opens model from given path and runs a function, passing the loaded Model
// flatbuffer as an argument.
//
// This is mainly useful if we don't want to pay the cost for the model
// initialization because we'll be only reading some flatbuffer values from the
// file.
template <typename ReturnType, typename Func>
ReturnType VisitActionsModel(const std::string& path, Func function) {
  ScopedMmap mmap(path);
  if (!mmap.handle().ok()) {
    function(/*model=*/nullptr);
  }
  const ActionsModel* model =
      ViewActionsModel(mmap.handle().start(), mmap.handle().num_bytes());
  return function(model);
}

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_SUGGESTIONS_H_
