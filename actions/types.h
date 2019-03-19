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

#ifndef LIBTEXTCLASSIFIER_ACTIONS_TYPES_H_
#define LIBTEXTCLASSIFIER_ACTIONS_TYPES_H_

#include <map>
#include <string>
#include <vector>

#include "actions/actions-entity-data_generated.h"
#include "annotator/types.h"
#include "utils/flatbuffers.h"

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

  // Span text.
  std::string text;

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
  std::string serialized_entity_data;

  const ActionsEntityData* entity_data() {
    return LoadAndVerifyFlatbuffer<ActionsEntityData>(
        serialized_entity_data.data(), serialized_entity_data.size());
  }
};

// Actions suggestions result containing meta - information and the suggested
// actions.
struct ActionsSuggestionsResponse {
  ActionsSuggestionsResponse()
      : sensitivity_score(-1),
        triggering_score(-1),
        output_filtered_sensitivity(false),
        output_filtered_min_triggering_score(false),
        output_filtered_low_confidence(false),
        output_filtered_locale_mismatch(false) {}

  // The sensitivity assessment.
  float sensitivity_score;
  float triggering_score;

  // Whether the output was suppressed by the sensitivity threshold.
  bool output_filtered_sensitivity;

  // Whether the output was suppressed by the triggering score threshold.
  bool output_filtered_min_triggering_score;

  // Whether the output was suppressed by the low confidence patterns.
  bool output_filtered_low_confidence;

  // Whether the output was suppressed due to locale mismatch.
  bool output_filtered_locale_mismatch;

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

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_ACTIONS_TYPES_H_
