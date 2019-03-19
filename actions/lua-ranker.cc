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

#include "actions/lua-ranker.h"
#include "utils/base/logging.h"
#include "utils/lua-utils.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "lauxlib.h"
#include "lualib.h"
#ifdef __cplusplus
}
#endif

namespace libtextclassifier3 {
namespace {

static constexpr const char* kTypeKey = "type";
static constexpr const char* kScoreKey = "score";
static constexpr const char* kResponseTextKey = "response_text";

}  // namespace

std::unique_ptr<ActionsSuggestionsLuaRanker>
ActionsSuggestionsLuaRanker::CreateActionsSuggestionsLuaRanker(
    const std::string& ranker_code, ActionsSuggestionsResponse* response) {
  auto ranker = std::unique_ptr<ActionsSuggestionsLuaRanker>(
      new ActionsSuggestionsLuaRanker(ranker_code, response));
  if (!ranker->InitializeEnvironment()) {
    TC3_LOG(ERROR) << "Could not initialize lua environment for ranker.";
    return nullptr;
  }
  return ranker;
}

bool ActionsSuggestionsLuaRanker::InitializeEnvironment() {
  return RunProtected([this] {
           LoadDefaultLibraries();

           // Setup callbacks.
           SetupTableLookupCallback("actions", CALLBACK_ID_ACTIONS);
           lua_setglobal(state_, "actions");

           return LUA_OK;
         }) == LUA_OK;
}

int ActionsSuggestionsLuaRanker::ReadActionsRanking() {
  if (lua_type(state_, /*idx=*/-1) != LUA_TTABLE) {
    TC3_LOG(ERROR) << "Expected actions table, got: "
                   << lua_type(state_, /*idx=*/-1);
    lua_pop(state_, 1);
    lua_error(state_);
    return LUA_ERRRUN;
  }
  std::vector<ActionSuggestion> ranked_actions;
  lua_pushnil(state_);
  while (lua_next(state_, /*idx=*/-2)) {
    const int action_id =
        static_cast<int>(lua_tointeger(state_, /*idx=*/-1)) - 1;
    lua_pop(state_, 1);
    if (action_id < 0 || action_id >= response_->actions.size()) {
      TC3_LOG(ERROR) << "Invalid action index: " << action_id;
      lua_error(state_);
      return LUA_ERRRUN;
    }
    ranked_actions.push_back(response_->actions[action_id]);
  }
  lua_pop(state_, 1);
  response_->actions = ranked_actions;
  return LUA_OK;
}

void ActionsSuggestionsLuaRanker::PushAction(const int action_id) {
  const ActionSuggestion action = response_->actions[action_id];
  lua_newtable(state_);
  PushString(action.type);
  lua_setfield(state_, /*idx=*/-2, kTypeKey);
  PushString(action.response_text);
  lua_setfield(state_, /*idx=*/-2, kResponseTextKey);
  lua_pushnumber(state_, action.score);
  lua_setfield(state_, /*idx=*/-2, kScoreKey);
}

int ActionsSuggestionsLuaRanker::HandleActionsCallback() {
  switch (lua_type(state_, -1)) {
    case LUA_TNUMBER: {
      // Lua is one based, so adjust the index here.
      const int action_id =
          static_cast<int>(lua_tonumber(state_, /*idx=*/-1)) - 1;
      if (action_id < 0 || action_id >= response_->actions.size()) {
        TC3_LOG(ERROR) << "Invalid action id: " << action_id;
        lua_error(state_);
        return 0;
      }
      PushAction(action_id);
      return /*num_results=*/1;
    }
    case LUA_TSTRING: {
      const StringPiece key = ReadString(/*index=*/-1);
      if (key.Equals(kLengthKey)) {
        lua_pushinteger(state_, response_->actions.size());
        return /*num results=*/1;
      } else if (key.Equals(kPairsKey)) {
        PushCallback(CALLBACK_ID_ACTIONS_ITER, /*args=*/{/*iterator=*/nullptr});
        return /*num_results=*/1;
      } else {
        TC3_LOG(ERROR) << "Undefined actions member access " << key.ToString();
        lua_error(state_);
        return 0;
      }
    }
    default:
      TC3_LOG(ERROR) << "Unexpected actions access type: "
                     << lua_type(state_, -1);
      lua_error(state_);
      return 0;
  }
}

int ActionsSuggestionsLuaRanker::HandleActionsIterCallback(
    const std::vector<void*>& args) {
  int64 it = reinterpret_cast<int64>(args[0]);
  if (it >= response_->actions.size()) {
    return 0;
  }

  // Update iterator value.
  lua_pushlightuserdata(state_, reinterpret_cast<void*>(it + 1));
  lua_replace(state_, GetArgIndex(0));

  // Key.
  lua_pushinteger(state_, it + 1);

  // Value.
  PushAction(it);

  return /*num results=*/2;
}

int ActionsSuggestionsLuaRanker::HandleCallback(
    const int callback_id, const std::vector<void*>& args) {
  switch (callback_id) {
    case CALLBACK_ID_ACTIONS:
      return HandleActionsCallback();
    case CALLBACK_ID_ACTIONS_ITER:
      return HandleActionsIterCallback(args);
    default:
      TC3_LOG(ERROR) << "Unhandled callback: " << callback_id;
      lua_error(state_);
      return 0;
  }
}

bool ActionsSuggestionsLuaRanker::RankActions() {
  if (response_->actions.empty()) {
    // Nothing to do.
    return true;
  }

  if (luaL_loadbuffer(state_, ranker_code_.data(), ranker_code_.size(),
                      /*name=*/nullptr) != LUA_OK) {
    TC3_LOG(ERROR) << "Could not load compiled ranking snippet.";
    return false;
  }

  if (lua_pcall(state_, /*nargs=*/0, /*nargs=*/1, /*errfunc=*/0) != LUA_OK) {
    TC3_LOG(ERROR) << "Could not run ranking snippet.";
    return false;
  }

  if (RunProtected([this] { return ReadActionsRanking(); }, /*num_args=*/1) !=
      LUA_OK) {
    TC3_LOG(ERROR) << "Could not read lua result.";
    return false;
  }
  return true;
}

}  // namespace libtextclassifier3
