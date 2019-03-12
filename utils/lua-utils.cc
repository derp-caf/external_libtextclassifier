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

// lua_dump takes an extra argument "strip" in 5.3, but not in 5.2.
#ifndef TC3_AOSP
#define lua_dump(L, w, d, s) lua_dump((L), (w), (d))
#endif

namespace libtextclassifier3 {
namespace {
// Upvalue indices for callbacks.
static const int kEnvIndex = 1;
static const int kCallbackIdIndex = 2;
static const int kNumArgsIndex = 3;
static const int kArgIndex = 4;

static const luaL_Reg defaultlibs[] = {{"_G", luaopen_base},
                                       {LUA_TABLIBNAME, luaopen_table},
                                       {LUA_STRLIBNAME, luaopen_string},
                                       {LUA_BITLIBNAME, luaopen_bit32},
                                       {LUA_MATHLIBNAME, luaopen_math},
                                       {nullptr, nullptr}};

// Implementation of a lua_Writer that appends the data to a string.
int LuaStringWriter(lua_State *state, const void *data, size_t size,
                    void *result) {
  std::string *const result_string = static_cast<std::string *>(result);
  result_string->insert(result_string->size(), static_cast<const char *>(data),
                        size);
  return LUA_OK;
}

}  // namespace

void LuaEnvironment::LoadDefaultLibraries() {
  for (const luaL_Reg *lib = defaultlibs; lib->func; lib++) {
    luaL_requiref(state_, lib->name, lib->func, 1);
    lua_pop(state_, 1); /* remove lib */
  }
}

int LuaEnvironment::LengthCallbackDispatch(lua_State *state) {
  lua_pushstring(state, kLengthKey);
  int num_results = CallbackDispatch(state);
  lua_remove(state, -2);
  return num_results;
}

int LuaEnvironment::PairsCallbackDispatch(lua_State *state) {
  lua_pushstring(state, kPairsKey);
  int num_results = CallbackDispatch(state);
  lua_remove(state, -2);
  return num_results;
}

int LuaEnvironment::CallbackDispatch(lua_State *state) {
  // Fetch reference to our environment.
  LuaEnvironment *env = static_cast<LuaEnvironment *>(
      lua_touserdata(state, lua_upvalueindex(kEnvIndex)));
  TC3_CHECK_EQ(env->state_, state);
  const int callback_id =
      lua_tointeger(state, lua_upvalueindex(kCallbackIdIndex));
  const int num_args = lua_tointeger(state, lua_upvalueindex(kNumArgsIndex));
  std::vector<void *> args(num_args);
  for (int i = 0; i < num_args; i++) {
    args[i] = lua_touserdata(state, GetArgIndex(i));
  }
  return env->HandleCallback(callback_id, args);
}

LuaEnvironment::LuaEnvironment() { state_ = luaL_newstate(); }

LuaEnvironment::~LuaEnvironment() {
  if (state_ != nullptr) {
    lua_close(state_);
  }
}

void LuaEnvironment::PushCallback(const int callback_id,
                                  const std::vector<void *> &args,
                                  const CallbackHandler handler) {
  lua_pushlightuserdata(state_, static_cast<void *>(this));
  lua_pushnumber(state_, callback_id);
  lua_pushnumber(state_, args.size());
  for (void *arg : args) {
    lua_pushlightuserdata(state_, arg);
  }
  lua_pushcclosure(state_, handler, 3 + args.size());
}

void LuaEnvironment::SetupTableLookupCallback(const char *name,
                                              const int callback_id,
                                              const std::vector<void *> &args) {
  lua_newtable(state_);
  luaL_newmetatable(state_, name);
  PushCallback(callback_id, args);
  lua_setfield(state_, -2, kIndexKey);
  PushCallback(callback_id, args, &LengthCallbackDispatch);
  lua_setfield(state_, -2, kLengthKey);
  PushCallback(callback_id, args, &PairsCallbackDispatch);
  lua_setfield(state_, -2, kPairsKey);
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
    lua_pushlstring(state_, value.StringValue().data(),
                    value.StringValue().size());
  } else {
    TC3_LOG(FATAL) << "Unknown value type.";
  }
}

StringPiece LuaEnvironment::ReadString(const int index) const {
  size_t length = 0;
  const char *data = lua_tolstring(state_, index, &length);
  return StringPiece(data, length);
}

void LuaEnvironment::PushString(const StringPiece str) {
  lua_pushlstring(state_, str.data(), str.size());
}

int LuaEnvironment::GetArgIndex(const int arg_index) {
  return lua_upvalueindex(kArgIndex + arg_index);
}

int LuaEnvironment::HandleCallback(int callback_id,
                                   const std::vector<void *> &) {
  lua_error(state_);
  return 0;
}

int LuaEnvironment::RunProtected(const std::function<int()> &func,
                                 const int num_args, const int num_results) {
  struct ProtectedCall {
    std::function<int()> func;

    static int run(lua_State *state) {
      // Read the pointer to the ProtectedCall struct.
      ProtectedCall *p = static_cast<ProtectedCall *>(
          lua_touserdata(state, lua_upvalueindex(1)));
      return p->func();
    }
  };
  ProtectedCall protected_call = {func};
  lua_pushlightuserdata(state_, &protected_call);
  lua_pushcclosure(state_, &ProtectedCall::run, /*n=*/1);
  // Put the closure before the arguments on the stack.
  if (num_args > 0) {
    lua_insert(state_, -(1 + num_args));
  }
  return lua_pcall(state_, num_args, num_results, /*errorfunc=*/0);
}

bool LuaEnvironment::Compile(StringPiece snippet, std::string *bytecode) {
  if (luaL_loadbuffer(state_, snippet.data(), snippet.size(),
                      /*name=*/nullptr) != LUA_OK) {
    TC3_LOG(ERROR) << "Could not compile lua snippet: "
                   << ReadString(/*index=*/-1).ToString();
    lua_pop(state_, 1);
    return false;
  }
  if (lua_dump(state_, LuaStringWriter, bytecode, /*strip*/ 1) != LUA_OK) {
    TC3_LOG(ERROR) << "Could not dump compiled lua snippet.";
    lua_pop(state_, 1);
    return false;
  }
  lua_pop(state_, 1);
  return true;
}

bool Compile(StringPiece snippet, std::string *bytecode) {
  return LuaEnvironment().Compile(snippet, bytecode);
}

}  // namespace libtextclassifier3
