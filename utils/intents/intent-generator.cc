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

#include "utils/intents/intent-generator.h"

#include <map>

#include "utils/base/logging.h"
#include "utils/java/string_utils.h"
#include "utils/lua-utils.h"
#include "utils/strings/stringpiece.h"
#include "utils/variant.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "lauxlib.h"
#include "lua.h"
#ifdef __cplusplus
}
#endif

namespace libtextclassifier3 {
namespace {

static constexpr const char* kEntityTextKey = "text";
static constexpr const char* kTimeUsecKey = "parsed_time_ms_utc";
static constexpr const char* kReferenceTimeUsecKey = "reference_time_ms_utc";

// An Android specific Lua environment with JNI backed callbacks.
class JniLuaEnvironment : public LuaEnvironment {
 public:
  JniLuaEnvironment(const JniCache* jni_cache, const jobject context,
                    StringPiece entity_text, int64 event_time_ms_usec,
                    int64 reference_time_ms_utc,
                    const std::map<std::string, Variant>& extra);

  // Runs an intent generator snippet.
  std::vector<RemoteActionTemplate> RunIntentGenerator(
      const std::string& generator_snippet);

 protected:
  int HandleCallback(int callback_id) override;

 private:
  // Callback handlers.
  int HandleExternalCallback();
  int HandleExtrasLookup();
  int HandleAndroidCallback();
  int HandleUserRestrictionsCallback();
  int HandleUrlEncode();
  int HandleUrlSchema();

  // Reads and create a RemoteAction result from Lua.
  RemoteActionTemplate ReadRemoteActionTemplateResult();

  // Reads the extras from the Lua result.
  void ReadExtras(std::map<std::string, Variant>* extra);

  // Reads the intent categories array from a Lua result.
  void ReadCategories(std::vector<std::string>* category);

  // Retrieves user manager if not previously done.
  bool RetrieveUserManager();

  // Builtins.
  enum CallbackId {
    CALLBACK_ID_EXTERNAL = 0,
    CALLBACK_ID_EXTRAS = 1,
    CALLBACK_ID_ANDROID = 2,
    CALLBACK_ID_USER_PERMISSIONS = 3,
    CALLBACK_ID_URL_ENCODE = 4,
    CALLBACK_ID_URL_SCHEMA = 5,
  };

  JNIEnv* jenv_;
  const JniCache* jni_cache_;
  jobject context_;
  StringPiece entity_text_;
  int64 event_time_ms_usec_;
  int64 reference_time_ms_utc_;
  const std::map<std::string, Variant>& extra_;

  ScopedGlobalRef<jobject> usermanager_;
  // Whether we previously attempted to retrieve the UserManager before.
  bool usermanager_retrieved_;
};

JniLuaEnvironment::JniLuaEnvironment(
    const JniCache* jni_cache, const jobject context, StringPiece entity_text,
    int64 event_time_ms_usec, int64 reference_time_ms_utc,
    const std::map<std::string, Variant>& extra)
    : jenv_(jni_cache ? jni_cache->GetEnv() : nullptr),
      jni_cache_(jni_cache),
      context_(context),
      entity_text_(entity_text),
      event_time_ms_usec_(event_time_ms_usec),
      reference_time_ms_utc_(reference_time_ms_utc),
      extra_(extra),
      usermanager_(/*object=*/nullptr,
                   /*jvm=*/(jni_cache ? jni_cache->jvm : nullptr)),
      usermanager_retrieved_(false) {
  LoadDefaultLibraries();

  // Setup callbacks.
  // This exposes an `external` object with the following fields:
  //   * extras: the bundle with all information about a classification.
  //   * android: callbacks into specific android provided methods.
  //   * android.user_restrictions: callbacks to check user permissions.
  SetupTableLookupCallback("external", CALLBACK_ID_EXTERNAL);

  // extras
  lua_pushstring(state_, "extras");
  SetupTableLookupCallback("extras", CALLBACK_ID_EXTRAS);
  lua_settable(state_, -3);

  // android
  lua_pushstring(state_, "android");
  SetupTableLookupCallback("android", CALLBACK_ID_ANDROID);

  // android.user_restrictions
  lua_pushstring(state_, "user_restrictions");
  SetupTableLookupCallback("user_restrictions", CALLBACK_ID_USER_PERMISSIONS);
  lua_settable(state_, -3);
  lua_settable(state_, -3);

  lua_setglobal(state_, "external");
}

int JniLuaEnvironment::HandleCallback(int callback_id) {
  switch (callback_id) {
    case CALLBACK_ID_EXTERNAL:
      return HandleExternalCallback();
    case CALLBACK_ID_EXTRAS:
      return HandleExtrasLookup();
    case CALLBACK_ID_ANDROID:
      return HandleAndroidCallback();
    case CALLBACK_ID_USER_PERMISSIONS:
      return HandleUserRestrictionsCallback();
    case CALLBACK_ID_URL_ENCODE:
      return HandleUrlEncode();
    case CALLBACK_ID_URL_SCHEMA:
      return HandleUrlSchema();
    default:
      TC3_LOG(ERROR) << "Unhandled callback: " << callback_id;
      return LUA_ERRRUN;
  }
}

int JniLuaEnvironment::HandleExternalCallback() {
  const char* key = luaL_checkstring(state_, 2);
  if (strcmp(kReferenceTimeUsecKey, key) == 0) {
    lua_pushinteger(state_, reference_time_ms_utc_);
    return LUA_YIELD;
  } else {
    TC3_LOG(ERROR) << "Undefined external access " << key;
    return LUA_ERRRUN;
  }
}

int JniLuaEnvironment::HandleExtrasLookup() {
  const char* key = luaL_checkstring(state_, 2);
  if (strcmp(kEntityTextKey, key) == 0) {
    lua_pushlstring(state_, entity_text_.data(), entity_text_.length());
  } else if (strcmp(kTimeUsecKey, key) == 0) {
    lua_pushinteger(state_, event_time_ms_usec_);
  } else {
    const auto it = extra_.find(std::string(key));
    if (it == extra_.end()) {
      TC3_LOG(ERROR) << "Undefined extra lookup " << key;
      return LUA_ERRRUN;
    }
    PushValue(it->second);
  }
  return LUA_YIELD;
}

int JniLuaEnvironment::HandleAndroidCallback() {
  const char* key = luaL_checkstring(state_, 2);
  if (strcmp("package_name", key) == 0) {
    ScopedLocalRef<jstring> package_name_str(
        static_cast<jstring>(jenv_->CallObjectMethod(
            context_, jni_cache_->context_get_package_name)));
    if (jni_cache_->ExceptionCheckAndClear()) {
      TC3_LOG(ERROR) << "Error calling Context.getPackageName";
      return LUA_ERRRUN;
    }
    ScopedStringChars package_name =
        GetScopedStringChars(jenv_, package_name_str.get());
    lua_pushstring(state_, reinterpret_cast<const char*>(package_name.get()));
    return LUA_YIELD;
  } else if (strcmp("urlencode", key) == 0) {
    PushCallback(CALLBACK_ID_URL_ENCODE);
    return LUA_YIELD;
  } else if (strcmp("url_schema", key) == 0) {
    PushCallback(CALLBACK_ID_URL_SCHEMA);
    return LUA_YIELD;
  } else {
    TC3_LOG(ERROR) << "Undefined android reference " << key;
    return LUA_ERRRUN;
  }
}

int JniLuaEnvironment::HandleUserRestrictionsCallback() {
  if (jni_cache_->usermanager_class == nullptr ||
      jni_cache_->usermanager_get_user_restrictions == nullptr) {
    // UserManager is only available for API level >= 17 and
    // getUserRestrictions only for API level >= 18, so we just return false
    // normally here.
    lua_pushboolean(state_, false);
    return LUA_YIELD;
  }

  // Get user manager if not previously retrieved.
  if (!RetrieveUserManager()) {
    TC3_LOG(ERROR) << "Error retrieving user manager.";
    return LUA_ERRRUN;
  }

  ScopedLocalRef<jobject> bundle(jenv_->CallObjectMethod(
      usermanager_.get(), jni_cache_->usermanager_get_user_restrictions));
  if (jni_cache_->ExceptionCheckAndClear() || bundle == nullptr) {
    TC3_LOG(ERROR) << "Error calling getUserRestrictions";
    return LUA_ERRRUN;
  }
  ScopedLocalRef<jstring> key(jenv_->NewStringUTF(luaL_checkstring(state_, 2)));
  if (key == nullptr) {
    TC3_LOG(ERROR) << "Expected string, got null.";
    return LUA_ERRRUN;
  }
  const bool permission = jenv_->CallBooleanMethod(
      bundle.get(), jni_cache_->bundle_get_boolean, key.get());
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error getting bundle value";
    lua_pushboolean(state_, false);
  } else {
    lua_pushboolean(state_, permission);
  }
  return LUA_YIELD;
}

int JniLuaEnvironment::HandleUrlEncode() {
  // Call Java URL encoder.
  ScopedLocalRef<jstring> input_str(
      jenv_->NewStringUTF(luaL_checkstring(state_, 1)));
  if (input_str == nullptr) {
    TC3_LOG(ERROR) << "Expected string, got null.";
    return LUA_ERRRUN;
  }
  ScopedLocalRef<jstring> encoding_str(jenv_->NewStringUTF("UTF-8"));
  ScopedLocalRef<jstring> encoded_str(
      static_cast<jstring>(jenv_->CallStaticObjectMethod(
          jni_cache_->urlencoder_class.get(), jni_cache_->urlencoder_encode,
          input_str.get(), encoding_str.get())));
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling UrlEncoder.encode";
    return LUA_ERRRUN;
  }
  ScopedStringChars encoded = GetScopedStringChars(jenv_, encoded_str.get());
  lua_pushstring(state_, encoded.get());
  return LUA_YIELD;
}

int JniLuaEnvironment::HandleUrlSchema() {
  // Call to Java URI parser.
  ScopedLocalRef<jstring> url_str(
      jenv_->NewStringUTF(luaL_checkstring(state_, 1)));
  if (url_str == nullptr) {
    TC3_LOG(ERROR) << "Expected string, got null";
    return LUA_ERRRUN;
  }
  // Try to parse uri and get scheme.
  ScopedLocalRef<jobject> uri(jenv_->CallStaticObjectMethod(
      jni_cache_->uri_class.get(), jni_cache_->uri_parse, url_str.get()));
  if (jni_cache_->ExceptionCheckAndClear() || uri == nullptr) {
    TC3_LOG(ERROR) << "Error calling Uri.parse";
    return LUA_ERRRUN;
  }
  ScopedLocalRef<jstring> scheme_str(static_cast<jstring>(
      jenv_->CallObjectMethod(uri.get(), jni_cache_->uri_get_scheme)));
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling Uri.getScheme";
    return LUA_ERRRUN;
  }
  if (scheme_str == nullptr) {
    lua_pushnil(state_);
  } else {
    ScopedStringChars scheme = GetScopedStringChars(jenv_, scheme_str.get());
    lua_pushstring(state_, scheme.get());
  }
  return LUA_YIELD;
}

bool JniLuaEnvironment::RetrieveUserManager() {
  if (context_ == nullptr) {
    return false;
  }
  if (usermanager_retrieved_) {
    return (usermanager_ != nullptr);
  }
  usermanager_retrieved_ = true;
  ScopedLocalRef<jstring> service(jenv_->NewStringUTF("user"));
  jobject usermanager_ref = jenv_->CallObjectMethod(
      context_, jni_cache_->context_get_system_service, service.get());
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling getSystemService.";
    return false;
  }
  usermanager_ = MakeGlobalRef(usermanager_ref, jenv_, jni_cache_->jvm);
  return (usermanager_ != nullptr);
}

RemoteActionTemplate JniLuaEnvironment::ReadRemoteActionTemplateResult() {
  RemoteActionTemplate result;
  // Read intent template.
  lua_pushnil(state_);
  while (lua_next(state_, /*idx=*/-2)) {
    const char* key = lua_tostring(state_, /*idx=*/-2);
    if (strcmp("title", key) == 0) {
      result.title = lua_tostring(state_, /*idx=*/-1);
    } else if (strcmp("description", key) == 0) {
      result.description = lua_tostring(state_, /*idx=*/-1);
    } else if (strcmp("action", key) == 0) {
      result.action = lua_tostring(state_, /*idx=*/-1);
    } else if (strcmp("data", key) == 0) {
      result.data = lua_tostring(state_, /*idx=*/-1);
    } else if (strcmp("type", key) == 0) {
      result.type = lua_tostring(state_, /*idx=*/-1);
    } else if (strcmp("flags", key) == 0) {
      result.flags = static_cast<int>(lua_tointeger(state_, /*idx=*/-1));
    } else if (strcmp("package_name", key) == 0) {
      result.package_name = lua_tostring(state_, /*idx=*/-1);
    } else if (strcmp("request_code", key) == 0) {
      result.request_code = static_cast<int>(lua_tointeger(state_, /*idx=*/-1));
    } else if (strcmp("category", key) == 0) {
      ReadCategories(&result.category);
    } else if (strcmp("extra", key) == 0) {
      ReadExtras(&result.extra);
    } else {
      TC3_LOG(INFO) << "Unknown entry: " << key;
    }
    lua_pop(state_, 1);
  }
  lua_pop(state_, 1);
  return result;
}

void JniLuaEnvironment::ReadCategories(std::vector<std::string>* category) {
  // Read category array.
  if (lua_type(state_, /*idx=*/-1) != LUA_TTABLE) {
    TC3_LOG(ERROR) << "Expected categories table, got: "
                   << lua_type(state_, /*idx=*/-1);
    lua_pop(state_, 1);
    return;
  }
  lua_pushnil(state_);
  while (lua_next(state_, /*idx=*/-2)) {
    category->push_back(lua_tostring(state_, /*idx=*/-1));
    lua_pop(state_, 1);
  }
}

void JniLuaEnvironment::ReadExtras(std::map<std::string, Variant>* extra) {
  if (lua_type(state_, /*idx=*/-1) != LUA_TTABLE) {
    TC3_LOG(ERROR) << "Expected extras table, got: "
                   << lua_type(state_, /*idx=*/-1);
    lua_pop(state_, 1);
    return;
  }
  lua_pushnil(state_);
  while (lua_next(state_, /*idx=*/-2)) {
    // Each entry is a table specifying name and value.
    // The value is specified via a type specific field as Lua doesn't allow
    // to easily distinguish between different number types.
    if (lua_type(state_, /*idx=*/-1) != LUA_TTABLE) {
      TC3_LOG(ERROR) << "Expected a table for an extra, got: "
                     << lua_type(state_, /*idx=*/-1);
      lua_pop(state_, 1);
      return;
    }
    std::string name;
    Variant value;

    lua_pushnil(state_);
    while (lua_next(state_, /*idx=*/-2)) {
      const char* key = lua_tostring(state_, /*idx=*/-2);
      if (strcmp("name", key) == 0) {
        name = std::string(lua_tostring(state_, /*idx=*/-1));
      } else if (strcmp("int_value", key) == 0) {
        value = Variant(static_cast<int>(lua_tonumber(state_, /*idx=*/-1)));
      } else if (strcmp("long_value", key) == 0) {
        value = Variant(static_cast<int64>(lua_tonumber(state_, /*idx=*/-1)));
      } else if (strcmp("float_value", key) == 0) {
        value = Variant(static_cast<float>(lua_tonumber(state_, /*idx=*/-1)));
      } else if (strcmp("bool_value", key) == 0) {
        value = Variant(static_cast<bool>(lua_toboolean(state_, /*idx=*/-1)));
      } else if (strcmp("string_value", key) == 0) {
        value = Variant(lua_tostring(state_, /*idx=*/-1));
      } else {
        TC3_LOG(INFO) << "Unknown extra field: " << key;
      }
      lua_pop(state_, 1);
    }
    if (!name.empty()) {
      (*extra)[name] = value;
    } else {
      TC3_LOG(ERROR) << "Unnamed extra entry. Skipping.";
    }
    lua_pop(state_, 1);
  }
}
}  // namespace

std::vector<RemoteActionTemplate> JniLuaEnvironment::RunIntentGenerator(
    const std::string& generator_snippet) {
  int status = luaL_loadstring(state_, generator_snippet.data());
  if (status != LUA_OK) {
    TC3_LOG(ERROR) << "Couldn't load generator snippet: " << status;
    return {};
  }
  status = lua_pcall(state_, /*nargs=*/0, /*nargs=*/1, /*errfunc=*/0);
  if (status != LUA_OK) {
    TC3_LOG(ERROR) << "Couldn't run generator snippet: " << status;
    return {};
  }
  // Read result.
  if (lua_gettop(state_) != 1 || lua_type(state_, 1) != LUA_TTABLE) {
    TC3_LOG(ERROR) << "Unexpected result for snippet.";
    return {};
  }

  // Read remote action templates array.
  std::vector<RemoteActionTemplate> result;
  lua_pushnil(state_);
  while (lua_next(state_, /*idx=*/-2)) {
    if (lua_type(state_, /*idx=*/-1) != LUA_TTABLE) {
      TC3_LOG(ERROR) << "Expected intent table, got: "
                     << lua_type(state_, /*idx=*/-1);
      lua_pop(state_, 1);
      continue;
    }
    result.push_back(ReadRemoteActionTemplateResult());
  }
  lua_pop(state_, /*n=*/1);

  // Check that we correctly cleaned-up the state.
  const int stack_size = lua_gettop(state_);
  if (stack_size > 0) {
    TC3_LOG(ERROR) << "Unexpected stack size.";
    lua_settop(state_, 0);
    return {};
  }

  return result;
}

IntentGenerator::IntentGenerator(const IntentFactoryModel* options,
                                 const std::shared_ptr<JniCache>& jni_cache,
                                 const jobject context)
    : options_(options), jni_cache_(jni_cache), context_(context) {
  if (options_ == nullptr || options_->entities() == nullptr) {
    return;
  }

  // Normally this check would be performed by the Java compiler and we wouldn't
  // need to worry about it here. But we can't depend on Android's SDK in Java,
  // so we check the instance type here.
  if (context != nullptr && !jni_cache->GetEnv()->IsInstanceOf(
                                context, jni_cache->context_class.get())) {
    TC3_LOG(ERROR) << "Provided context is not an android.content.Context";
    return;
  }

  if (options_ != nullptr && options_->entities() != nullptr) {
    for (const IntentFactoryModel_::IntentGenerator* generator :
         *options_->entities()) {
      generators_[generator->entity_type()->str()] =
          std::string(reinterpret_cast<const char*>(
                          generator->lua_template_generator()->data()),
                      generator->lua_template_generator()->size());
    }
  }
}

std::vector<RemoteActionTemplate> IntentGenerator::GenerateIntents(
    const ClassificationResult& classification, int64 reference_time_ms_usec,
    StringPiece entity_text) const {
  if (options_ == nullptr) {
    return {};
  }

  // Retrieve generator for specified entity.
  auto it = generators_.find(classification.collection);
  if (it == generators_.end()) {
    TC3_LOG(INFO) << "Unknown entity: " << classification.collection;
    return {};
  }

  std::unique_ptr<JniLuaEnvironment> interpreter(
      new JniLuaEnvironment(jni_cache_.get(), context_, entity_text,
                            classification.datetime_parse_result.time_ms_utc,
                            reference_time_ms_usec, classification.extra));

  return interpreter->RunIntentGenerator(it->second);
}

}  // namespace libtextclassifier3
