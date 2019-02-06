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

#ifndef LIBTEXTCLASSIFIER_UTILS_LUA_UTILS_H_
#define LIBTEXTCLASSIFIER_UTILS_LUA_UTILS_H_

#include "utils/strings/stringpiece.h"
#include "utils/variant.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "lua.h"
#ifdef __cplusplus
}
#endif

namespace libtextclassifier3 {

class LuaEnvironment {
 public:
  virtual ~LuaEnvironment();
  LuaEnvironment();

 protected:
  // Loads default libraries.
  void LoadDefaultLibraries();

  // Provides a callback to Lua with given id, which will be dispatched to
  // `HandleCallback(id)` when called. This is useful when we need to call
  // native C++ code from within Lua code.
  void PushCallback(int callback_id);

  // Setup a named table that callsback whenever a member is accessed.
  // This allows to lazily provide required information to the script.
  // `HandleCallback` will be called upon callback invocation with the
  // callback identifier provided.
  void SetupTableLookupCallback(const char *name, int callback_id);

  // Called from Lua when invoking a callback either by
  // `PushCallback` or `SetupTableLookupCallback`.
  virtual int HandleCallback(int callback_id);

  void PushValue(const Variant &value);

  lua_State *state_;

 private:
  static int CallbackDispatch(lua_State *state);
};

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_UTILS_LUA_UTILS_H_
