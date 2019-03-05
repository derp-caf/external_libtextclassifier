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

#include "actions/ranker.h"
#include "actions/lua-ranker.h"
#include "utils/base/logging.h"
#include "utils/lua-utils.h"

namespace libtextclassifier3 {
namespace {

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
      action.annotations.size() != other.annotations.size() ||
      action.serialized_entity_data != other.serialized_entity_data) {
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

std::unique_ptr<ActionsSuggestionsRanker>
ActionsSuggestionsRanker::CreateActionsSuggestionsRanker(
    const RankingOptions* options) {
  auto ranker = std::unique_ptr<ActionsSuggestionsRanker>(
      new ActionsSuggestionsRanker(options));

  if (!ranker->InitializeAndValidate()) {
    TC3_LOG(ERROR) << "Could not initialize action ranker.";
    return nullptr;
  }

  return ranker;
}

bool ActionsSuggestionsRanker::InitializeAndValidate() {
  if (options_ == nullptr) {
    TC3_LOG(ERROR) << "No ranking options specified.";
    return false;
  }

  if (options_->lua_ranking_script() != nullptr &&
      !Compile(options_->lua_ranking_script()->str(), &lua_bytecode_)) {
    TC3_LOG(ERROR) << "Could not precompile lua ranking snippet.";
    return false;
  }

  return true;
}

bool ActionsSuggestionsRanker::RankActions(
    ActionsSuggestionsResponse* response) const {
  // First order suggestions by score.
  std::sort(response->actions.begin(), response->actions.end(),
            [](const ActionSuggestion& a, const ActionSuggestion& b) {
              return a.score > b.score;
            });

  // Deduplicate, keeping the higher score actions.
  if (options_->deduplicate_suggestions()) {
    std::vector<ActionSuggestion> deduplicated_actions;
    for (const ActionSuggestion& candidate : response->actions) {
      // Check whether we already have an equivalent action.
      if (!IsAnyActionEquivalent(candidate, deduplicated_actions)) {
        deduplicated_actions.push_back(candidate);
      }
    }
    response->actions.swap(deduplicated_actions);
  }

  // Run lua ranking snippet, if provided.
  if (!lua_bytecode_.empty()) {
    auto lua_ranker =
        ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
            lua_bytecode_, response);
    if (lua_ranker == nullptr || !lua_ranker->RankActions()) {
      TC3_LOG(ERROR) << "Could not run lua ranking snippet.";
      return false;
    }
  }

  return true;
}

}  // namespace libtextclassifier3
