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

#ifndef LIBTEXTCLASSIFIER_ACTIONS_RANKER_H_
#define LIBTEXTCLASSIFIER_ACTIONS_RANKER_H_

#include <memory>

#include "actions/actions_model_generated.h"
#include "actions/types.h"

namespace libtextclassifier3 {

// Ranking and filtering of actions suggestions.
class ActionsSuggestionsRanker {
 public:
  static std::unique_ptr<ActionsSuggestionsRanker>
  CreateActionsSuggestionsRanker(const RankingOptions* options);

  // Rank and filter actions.
  bool RankActions(ActionsSuggestionsResponse* response) const;

 private:
  explicit ActionsSuggestionsRanker(const RankingOptions* options)
      : options_(options) {}

  bool InitializeAndValidate();

  const RankingOptions* const options_;
  std::string lua_bytecode_;
};

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_ACTIONS_RANKER_H_
