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
static const int kEnvIndex = 1;
static const int kCallbackIdIndex = 2;

static const luaL_Reg defaultlibs[] = {{"_G", luaopen_base},
                                       {LUA_TABLIBNAME, luaopen_table},
                                       {LUA_STRLIBNAME, luaopen_string},
                                       {LUA_BITLIBNAME, luaopen_bit32},
                                       {LUA_MATHLIBNAME, luaopen_math},
                                       {nullptr, nullptr}};

}  // namespace

void LuaEnvironment::LoadDefaultLibraries() {
  for (const luaL_Reg *lib = defaultlibs; lib->func; lib++) {
    luaL_requiref(state_, lib->name, lib->func, 1);
    lua_pop(state_, 1); /* remove lib */
  }
}

int LuaEnvironment::CallbackDispatch(lua_State *state) {
  // Fetch reference to our environment.
  LuaEnvironment *env = static_cast<LuaEnvironment *>(
      lua_touserdata(state, lua_upvalueindex(kEnvIndex)));
  TC3_CHECK_EQ(env->state_, state);
  int callback_id = lua_tointeger(state, lua_upvalueindex(kCallbackIdIndex));
  return env->HandleCallback(callback_id);
}

LuaEnvironment::LuaEnvironment() { state_ = luaL_newstate(); }

LuaEnvironment::~LuaEnvironment() {
  if (state_ != nullptr) {
    lua_close(state_);
  }
}

void LuaEnvironment::PushCallback(int callback_id) {
  lua_pushlightuserdata(state_, static_cast<void *>(this));
  lua_pushnumber(state_, callback_id);
  lua_pushcclosure(state_, CallbackDispatch, 2);
}

void LuaEnvironment::SetupTableLookupCallback(const char *name,
                                              int callback_id) {
  lua_newtable(state_);
  luaL_newmetatable(state_, name);
  PushCallback(callback_id);
  lua_setfield(state_, -2, "__index");
  lua_setmetatable(state_, -2);
}

void LuaEnvironment::PushValue(const Variant &value) {
  if (value.HasInt()) {
    lua_pushnumber(state_, value.IntValue());
  } else if (value.HasInt64()) {
    lua_pushnumber(state_, value.Int64Value());
  } else if (value.HasBool()) {
    lua_pushboolean(state_, value.BoolValue());
  } else if (value.HasFloat()) {
    lua_pushnumber(state_, value.FloatValue());
  } else if (value.HasDouble()) {
    lua_pushnumber(state_, value.DoubleValue());
  } else if (value.HasString()) {
    lua_pushstring(state_, value.StringValue().data());
  } else {
    TC3_LOG(FATAL) << "Unknown value type.";
  }
}

int LuaEnvironment::HandleCallback(int callback_id) { return LUA_ERRRUN; }

}  // namespace libtextclassifier3
