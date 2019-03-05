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
#include "actions/ranker.h"
#include "actions/types.h"
#include "annotator/annotator.h"
#include "annotator/types.h"
#include "utils/flatbuffers.h"
#include "utils/i18n/locale.h"
#include "utils/memory/mmap.h"
#include "utils/tflite-model-executor.h"
#include "utils/utf8/unilib.h"
#include "utils/variant.h"
#include "utils/zlib/zlib.h"

namespace libtextclassifier3 {

// Options for suggesting actions.
struct ActionSuggestionOptions {
  // Options for annotation of the messages.
  AnnotationOptions annotation_options = AnnotationOptions();

  bool ignore_min_replies_triggering_threshold = false;

  // UTC milliseconds since epoch.
  // This can currently be used during intent generation.
  int64 reference_time_ms_utc = 0;

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
      const ActionSuggestionOptions& options = ActionSuggestionOptions()) const;

  ActionsSuggestionsResponse SuggestActions(
      const Conversation& conversation, const Annotator* annotator,
      const ActionSuggestionOptions& options = ActionSuggestionOptions()) const;

  const ActionsModel* model() const;
  const reflection::Schema* entity_data_schema() const;

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
  struct CompiledRule {
    const RulesModel_::Rule* rule;
    std::unique_ptr<UniLib::RegexPattern> pattern;
  };

  // Checks that model contains all required fields, and initializes internal
  // datastructures.
  bool ValidateAndInitialize();

  void SetOrCreateUnilib(const UniLib* unilib);

  // Initializes regular expression rules.
  bool InitializeRules(ZlibDecompressor* decompressor);
  bool InitializeRules(ZlibDecompressor* decompressor, const RulesModel* rules,
                       std::vector<CompiledRule>* compiled_rules) const;

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

  void CreateActionsFromAnnotation(
      const int message_index, const ActionSuggestionAnnotation& annotation,
      ActionsSuggestionsResponse* suggestions) const;

  // Deduplicates equivalent annotations - annotations that have the same type
  // and same span text.
  // Returns the indices of the deduplicated annotations.
  std::vector<int> DeduplicateAnnotations(
      const std::vector<ActionSuggestionAnnotation>& annotations) const;

  bool SuggestActionsFromRules(const Conversation& conversation,
                               ActionsSuggestionsResponse* suggestions) const;

  bool GatherActionsSuggestions(const Conversation& conversation,
                                const Annotator* annotator,
                                const ActionSuggestionOptions& options,
                                ActionsSuggestionsResponse* response) const;

  // Checks whether a locale matches any of the model locales.
  bool IsLocaleSupportedByModel(const Locale& locale) const;
  bool IsAnyLocaleSupportedByModel(const std::vector<Locale>& locales) const;

  // Checks whether the input triggers the low confidence checks.
  bool IsLowConfidenceInput(const Conversation& conversation,
                            const int num_messages) const;

  // Returns whether a regex rule provides entity data from a match.
  bool HasEntityData(const RulesModel_::Rule* rule) const;

  const ActionsModel* model_;
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap_;

  // Tensorflow Lite models.
  std::unique_ptr<const TfLiteModelExecutor> model_executor_;

  // Rules.
  std::vector<CompiledRule> rules_, low_confidence_rules_;

  std::unique_ptr<UniLib> owned_unilib_;
  const UniLib* unilib_;

  // Locales supported by the model.
  std::vector<Locale> locales_;

  // Builder for creating extra data.
  const reflection::Schema* entity_data_schema_;
  std::unique_ptr<ReflectiveFlatbufferBuilder> entity_data_builder_;
  std::unique_ptr<ActionsSuggestionsRanker> ranker_;
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
