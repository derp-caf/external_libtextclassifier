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

#ifndef LIBTEXTCLASSIFIER_ACTIONS_LUA_RANKER_H_
#define LIBTEXTCLASSIFIER_ACTIONS_LUA_RANKER_H_

#include <memory>
#include <string>

#include "actions/types.h"
#include "utils/lua-utils.h"

namespace libtextclassifier3 {

// Lua backed action suggestion ranking.
class ActionsSuggestionsLuaRanker : public LuaEnvironment {
 public:
  static std::unique_ptr<ActionsSuggestionsLuaRanker>
  CreateActionsSuggestionsLuaRanker(const std::string& ranker_code,
                                    ActionsSuggestionsResponse* response);

  bool RankActions();

 private:
  enum CallbackId {
    CALLBACK_ID_ACTIONS = 0,
    CALLBACK_ID_ACTIONS_ITER = 1,
  };

  explicit ActionsSuggestionsLuaRanker(const std::string& ranker_code,
                                       ActionsSuggestionsResponse* response)
      : ranker_code_(ranker_code), response_(response) {}

  bool InitializeEnvironment();

  // Callback handlers. The return value is the number of results pushed.
  int HandleCallback(const int callback_id,
                     const std::vector<void*>& args) override;
  // Handles callbacks into the `actions` global provided in
  // `InitializeEnvironment`. It handles index based lookups into the actions
  // array, as well as iteration and length lookup.
  int HandleActionsCallback();
  int HandleActionsIterCallback(const std::vector<void*>& args);

  // Pushes an annotation as table on the lua stack.
  void PushAction(const int action_id);

  // Reads ranking results from the lua stack.
  int ReadActionsRanking();

  const std::string& ranker_code_;
  ActionsSuggestionsResponse* response_;
};

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_ACTIONS_LUA_RANKER_H_
