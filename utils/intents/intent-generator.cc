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

#include "annotator/types.h"
#include "utils/base/logging.h"
#include "utils/hash/farmhash.h"
#include "utils/java/jni-base.h"
#include "utils/java/string_utils.h"
#include "utils/lua-utils.h"
#include "utils/strings/stringpiece.h"
#include "utils/utf8/unicodetext.h"
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

static constexpr const char* kTextKey = "text";
static constexpr const char* kTimeUsecKey = "parsed_time_ms_utc";
static constexpr const char* kReferenceTimeUsecKey = "reference_time_ms_utc";
static constexpr const char* kHashKey = "hash";
static constexpr const char* kUrlSchemaKey = "url_schema";
static constexpr const char* kUrlHostKey = "url_host";
static constexpr const char* kUrlEncodeKey = "urlencode";
static constexpr const char* kPackageNameKey = "package_name";
static constexpr const char* kDeviceLocaleKey = "device_locale";

std::string ExtractUtf8Substring(const std::string& text,
                                 const CodepointSpan& span) {
  const UnicodeText text_unicode = UTF8ToUnicodeText(text,
                                                     /*do_copy=*/false);
  auto span_begin = text_unicode.begin();
  std::advance(span_begin, span.first);
  auto span_end = text_unicode.begin();
  std::advance(span_end, span.second);
  return UnicodeText::UTF8Substring(span_begin, span_end);
}

// An Android specific Lua environment with JNI backed callbacks.
class JniLuaEnvironment : public LuaEnvironment {
 public:
  static std::unique_ptr<JniLuaEnvironment> CreateJniLuaEnvironment(
      const Resources& resources, const JniCache* jni_cache,
      const jobject context, const Locale& device_locale,
      const StringPiece entity_text, const int64 event_time_ms_utc,
      const int64 reference_time_ms_utc,
      const std::map<std::string, Variant>& extra);

  // Runs an intent generator snippet.
  std::vector<RemoteActionTemplate> RunIntentGenerator(
      const std::string& generator_snippet);

 protected:
  JniLuaEnvironment(const Resources& resources, const JniCache* jni_cache,
                    const jobject context, const Locale& device_locale,
                    const StringPiece entity_text,
                    const int64 event_time_ms_utc,
                    const int64 reference_time_ms_utc,
                    const std::map<std::string, Variant>& extra);

  // Environment setup.
  bool PrepareEnvironment();
  virtual void SetupExternalHook();

  // Callback handlers.
  int HandleCallback(int callback_id, void*) override;
  int HandleExternalCallback();
  int HandleExtrasLookup();
  int HandleAndroidCallback();
  int HandleUserRestrictionsCallback();
  int HandleUrlEncode();
  int HandleUrlSchema();
  int HandleHash();
  int HandleAndroidStringResources();
  int HandleUrlHost();

  // Checks and retrieves string resources from the model.
  bool LookupModelStringResource();

  // Reads and create a RemoteAction result from Lua.
  RemoteActionTemplate ReadRemoteActionTemplateResult();

  // Reads the extras from the Lua result.
  void ReadExtras(std::map<std::string, Variant>* extra);

  // Reads the intent categories array from a Lua result.
  void ReadCategories(std::vector<std::string>* category);

  // Retrieves user manager if not previously done.
  bool RetrieveUserManager();

  // Retrieves system resources if not previously done.
  bool RetrieveSystemResources();

  // Parse the url string by using Uri.parse from Java.
  ScopedLocalRef<jobject> ParseUri(StringPiece& url) const;

  // Builtins.
  enum CallbackId {
    CALLBACK_ID_EXTERNAL = 0,
    CALLBACK_ID_EXTRAS = 1,
    CALLBACK_ID_ANDROID = 2,
    CALLBACK_ID_USER_PERMISSIONS = 3,
    CALLBACK_ID_URL_ENCODE = 4,
    CALLBACK_ID_URL_SCHEMA = 5,
    CALLBACK_ID_ANDROID_STRING_RESOURCES = 6,
    CALLBACK_ID_HASH = 7,
    CALLBACK_ID_ANNOTATION = 8,
    CALLBACK_ID_ANNOTATION_EXTRAS = 9,
    CALLBACK_ID_URL_HOST = 10,
  };

  const Resources& resources_;
  JNIEnv* jenv_;
  const JniCache* jni_cache_;
  const jobject context_;
  const Locale& device_locale_;
  const StringPiece entity_text_;
  const int64 event_time_ms_utc_;
  const int64 reference_time_ms_utc_;
  const std::map<std::string, Variant>& extra_;

  ScopedGlobalRef<jobject> usermanager_;
  // Whether we previously attempted to retrieve the UserManager before.
  bool usermanager_retrieved_;

  ScopedGlobalRef<jobject> system_resources_;
  // Whether we previously attempted to retrieve the system resources.
  bool system_resources_resources_retrieved_;

  // Cached JNI references for Java strings `string` and `android`.
  ScopedGlobalRef<jstring> string_;
  ScopedGlobalRef<jstring> android_;
};

// An Android specific Lua environment with JNI backed callbacks with action
// suggestion specific extensions.
class ActionsJniLuaEnvironment : public JniLuaEnvironment {
 public:
  static std::unique_ptr<ActionsJniLuaEnvironment>
  CreateActionsJniLuaEnvironment(const Resources& resources,
                                 const JniCache* jni_cache,
                                 const jobject context,
                                 const Locale& device_locale,
                                 const Conversation& conversation,
                                 const ActionSuggestion& action,
                                 const int64 reference_time_ms_utc);

 protected:
  ActionsJniLuaEnvironment(const Resources& resources,
                           const JniCache* jni_cache, const jobject context,
                           const Locale& device_locale,
                           const Conversation& conversation,
                           const ActionSuggestion& action,
                           const int64 reference_time_ms_utc);

  void SetupExternalHook() override;

  int HandleCallback(int callback_id, void* callback_arg) override;
  int HandleAnnotationCallback();
  int HandleAnnotationExtrasCallback(void* callback_arg);

  const Conversation& conversation_;
  const ActionSuggestion& action_;
};

std::unique_ptr<JniLuaEnvironment> JniLuaEnvironment::CreateJniLuaEnvironment(
    const Resources& resources, const JniCache* jni_cache,
    const jobject context, const Locale& device_locale,
    const StringPiece entity_text, const int64 event_time_ms_utc,
    const int64 reference_time_ms_utc,
    const std::map<std::string, Variant>& extra) {
  if (jni_cache == nullptr) {
    return nullptr;
  }
  auto lua_env = std::unique_ptr<JniLuaEnvironment>(new JniLuaEnvironment(
      resources, jni_cache, context, device_locale, entity_text,
      event_time_ms_utc, reference_time_ms_utc, extra));
  if (!lua_env->PrepareEnvironment()) {
    return nullptr;
  }
  return lua_env;
}

JniLuaEnvironment::JniLuaEnvironment(
    const Resources& resources, const JniCache* jni_cache,
    const jobject context, const Locale& device_locale,
    const StringPiece entity_text, const int64 event_time_ms_utc,
    const int64 reference_time_ms_utc,
    const std::map<std::string, Variant>& extra)
    : resources_(resources),
      jenv_(jni_cache ? jni_cache->GetEnv() : nullptr),
      jni_cache_(jni_cache),
      context_(context),
      device_locale_(device_locale),
      entity_text_(entity_text),
      event_time_ms_utc_(event_time_ms_utc),
      reference_time_ms_utc_(reference_time_ms_utc),
      extra_(extra),
      usermanager_(/*object=*/nullptr,
                   /*jvm=*/(jni_cache ? jni_cache->jvm : nullptr)),
      usermanager_retrieved_(false),
      system_resources_(/*object=*/nullptr,
                        /*jvm=*/(jni_cache ? jni_cache->jvm : nullptr)),
      system_resources_resources_retrieved_(false),
      string_(/*object=*/nullptr,
              /*jvm=*/(jni_cache ? jni_cache->jvm : nullptr)),
      android_(/*object=*/nullptr,
               /*jvm=*/(jni_cache ? jni_cache->jvm : nullptr)) {}

bool JniLuaEnvironment::PrepareEnvironment() {
  string_ =
      MakeGlobalRef(jenv_->NewStringUTF("string"), jenv_, jni_cache_->jvm);
  android_ =
      MakeGlobalRef(jenv_->NewStringUTF("android"), jenv_, jni_cache_->jvm);
  if (string_ == nullptr || android_ == nullptr) {
    TC3_LOG(ERROR) << "Could not allocate constant strings references.";
    return false;
  }
  LoadDefaultLibraries();
  SetupExternalHook();
  lua_setglobal(state_, "external");
  return true;
}

void JniLuaEnvironment::SetupExternalHook() {
  // This exposes an `external` object with the following fields:
  //   * extras: the bundle with all information about a classification.
  //   * android: callbacks into specific android provided methods.
  //   * android.user_restrictions: callbacks to check user permissions.
  //   * android.R: callbacks to retrieve string resources.
  SetupTableLookupCallback("external", CALLBACK_ID_EXTERNAL);

  // extras
  PushString("extras");
  SetupTableLookupCallback("extras", CALLBACK_ID_EXTRAS);
  lua_settable(state_, /*idx=*/-3);

  // android
  PushString("android");
  SetupTableLookupCallback("android", CALLBACK_ID_ANDROID);
  {
    // android.user_restrictions
    PushString("user_restrictions");
    SetupTableLookupCallback("user_restrictions", CALLBACK_ID_USER_PERMISSIONS);
    lua_settable(state_, /*idx=*/-3);

    // android.R
    // Callback to access android string resources.
    PushString("R");
    SetupTableLookupCallback("R", CALLBACK_ID_ANDROID_STRING_RESOURCES);
    lua_settable(state_, /*idx=*/-3);
  }
  lua_settable(state_, /*idx=*/-3);
}

int JniLuaEnvironment::HandleCallback(int callback_id, void*) {
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
    case CALLBACK_ID_ANDROID_STRING_RESOURCES:
      return HandleAndroidStringResources();
    case CALLBACK_ID_HASH:
      return HandleHash();
    case CALLBACK_ID_URL_HOST:
      return HandleUrlHost();
    default:
      TC3_LOG(ERROR) << "Unhandled callback: " << callback_id;
      lua_error(state_);
      return LUA_ERRRUN;
  }
}

int JniLuaEnvironment::HandleExternalCallback() {
  const StringPiece key = ReadString(/*index=*/-1);
  if (key.Equals(kReferenceTimeUsecKey)) {
    lua_pushinteger(state_, reference_time_ms_utc_);
    return LUA_YIELD;
  } else if (key.Equals(kHashKey)) {
    PushCallback(CALLBACK_ID_HASH);
    return LUA_YIELD;
  } else {
    TC3_LOG(ERROR) << "Undefined external access " << key.ToString();
    lua_error(state_);
    return LUA_ERRRUN;
  }
}

int JniLuaEnvironment::HandleExtrasLookup() {
  const StringPiece key = ReadString(/*index=*/-1);
  if (key.Equals(kTextKey)) {
    PushString(entity_text_);
  } else if (key.Equals(kTimeUsecKey)) {
    lua_pushinteger(state_, event_time_ms_utc_);
  } else {
    const auto it = extra_.find(key.ToString());
    if (it == extra_.end()) {
      TC3_LOG(ERROR) << "Undefined extra lookup " << key.ToString();
      lua_error(state_);
      return LUA_ERRRUN;
    }
    PushValue(it->second);
  }
  return LUA_YIELD;
}

int JniLuaEnvironment::HandleAndroidCallback() {
  const StringPiece key = ReadString(/*index=*/-1);
  if (key.Equals(kDeviceLocaleKey)) {
    // Provide the locale as table with the individual fields set.
    lua_newtable(state_);
    PushString(device_locale_.Language());
    lua_setfield(state_, -2, "language");
    PushString(device_locale_.Region());
    lua_setfield(state_, -2, "region");
    PushString(device_locale_.Script());
    lua_setfield(state_, -2, "script");
    return LUA_YIELD;
  } else if (key.Equals(kPackageNameKey)) {
    if (context_ == nullptr) {
      TC3_LOG(ERROR) << "Context invalid.";
      lua_error(state_);
      return LUA_ERRRUN;
    }
    ScopedLocalRef<jstring> package_name_str(
        static_cast<jstring>(jenv_->CallObjectMethod(
            context_, jni_cache_->context_get_package_name)));
    if (jni_cache_->ExceptionCheckAndClear()) {
      TC3_LOG(ERROR) << "Error calling Context.getPackageName";
      lua_error(state_);
      return LUA_ERRRUN;
    }
    PushString(ToStlString(jenv_, package_name_str.get()));
    return LUA_YIELD;
  } else if (key.Equals(kUrlEncodeKey)) {
    PushCallback(CALLBACK_ID_URL_ENCODE);
    return LUA_YIELD;
  } else if (key.Equals(kUrlHostKey)) {
    PushCallback(CALLBACK_ID_URL_HOST);
    return LUA_YIELD;
  } else if (key.Equals(kUrlSchemaKey)) {
    PushCallback(CALLBACK_ID_URL_SCHEMA);
    return LUA_YIELD;
  } else {
    TC3_LOG(ERROR) << "Undefined android reference " << key.ToString();
    lua_error(state_);
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
    lua_error(state_);
    return LUA_ERRRUN;
  }

  ScopedLocalRef<jobject> bundle(jenv_->CallObjectMethod(
      usermanager_.get(), jni_cache_->usermanager_get_user_restrictions));
  if (jni_cache_->ExceptionCheckAndClear() || bundle == nullptr) {
    TC3_LOG(ERROR) << "Error calling getUserRestrictions";
    lua_error(state_);
    return LUA_ERRRUN;
  }

  const StringPiece key_str = ReadString(/*index=*/-1);
  if (key_str.empty()) {
    TC3_LOG(ERROR) << "Expected string, got null.";
    lua_error(state_);
    return LUA_ERRRUN;
  }

  ScopedLocalRef<jstring> key = jni_cache_->ConvertToJavaString(key_str);
  if (jni_cache_->ExceptionCheckAndClear() || key == nullptr) {
    TC3_LOG(ERROR) << "Expected string, got null.";
    lua_error(state_);
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
  const StringPiece input = ReadString(/*index=*/1);
  if (input.empty()) {
    TC3_LOG(ERROR) << "Expected string, got null.";
    lua_error(state_);
    return LUA_ERRRUN;
  }

  // Call Java URL encoder.
  ScopedLocalRef<jstring> input_str = jni_cache_->ConvertToJavaString(input);
  if (jni_cache_->ExceptionCheckAndClear() || input_str == nullptr) {
    TC3_LOG(ERROR) << "Expected string, got null.";
    lua_error(state_);
    return LUA_ERRRUN;
  }
  ScopedLocalRef<jstring> encoded_str(
      static_cast<jstring>(jenv_->CallStaticObjectMethod(
          jni_cache_->urlencoder_class.get(), jni_cache_->urlencoder_encode,
          input_str.get(), jni_cache_->string_utf8.get())));
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling UrlEncoder.encode";
    lua_error(state_);
    return LUA_ERRRUN;
  }
  PushString(ToStlString(jenv_, encoded_str.get()));
  return LUA_YIELD;
}

ScopedLocalRef<jobject> JniLuaEnvironment::ParseUri(StringPiece& url) const {
  if (url.empty()) {
    return nullptr;
  }

  // Call to Java URI parser.
  ScopedLocalRef<jstring> url_str = jni_cache_->ConvertToJavaString(url);
  if (jni_cache_->ExceptionCheckAndClear() || url_str == nullptr) {
    TC3_LOG(ERROR) << "Expected string, got null";
    return nullptr;
  }

  // Try to parse uri and get scheme.
  ScopedLocalRef<jobject> uri(jenv_->CallStaticObjectMethod(
      jni_cache_->uri_class.get(), jni_cache_->uri_parse, url_str.get()));
  if (jni_cache_->ExceptionCheckAndClear() || uri == nullptr) {
    TC3_LOG(ERROR) << "Error calling Uri.parse";
  }
  return uri;
}

int JniLuaEnvironment::HandleUrlSchema() {
  StringPiece url = ReadString(/*index=*/1);

  ScopedLocalRef<jobject> parsed_uri = ParseUri(url);
  if (parsed_uri == nullptr) {
    lua_error(state_);
    return LUA_ERRRUN;
  }

  ScopedLocalRef<jstring> scheme_str(static_cast<jstring>(
      jenv_->CallObjectMethod(parsed_uri.get(), jni_cache_->uri_get_scheme)));
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling Uri.getScheme";
    lua_error(state_);
    return LUA_ERRRUN;
  }
  if (scheme_str == nullptr) {
    lua_pushnil(state_);
  } else {
    PushString(ToStlString(jenv_, scheme_str.get()));
  }
  return LUA_YIELD;
}

int JniLuaEnvironment::HandleUrlHost() {
  StringPiece url = ReadString(/*index=*/-1);

  ScopedLocalRef<jobject> parsed_uri = ParseUri(url);
  if (parsed_uri == nullptr) {
    lua_error(state_);
    return LUA_ERRRUN;
  }

  ScopedLocalRef<jstring> host_str(static_cast<jstring>(
      jenv_->CallObjectMethod(parsed_uri.get(), jni_cache_->uri_get_host)));
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling Uri.getHost";
    lua_error(state_);
    return LUA_ERRRUN;
  }
  if (host_str == nullptr) {
    lua_pushnil(state_);
  } else {
    PushString(ToStlString(jenv_, host_str.get()));
  }
  return LUA_YIELD;
}

int JniLuaEnvironment::HandleHash() {
  const StringPiece input = ReadString(/*index=*/-1);
  lua_pushinteger(state_, tc3farmhash::Hash32(input.data(), input.length()));
  return LUA_YIELD;
}

bool JniLuaEnvironment::LookupModelStringResource() {
  // Handle only lookup by name.
  if (lua_type(state_, 2) != LUA_TSTRING) {
    return false;
  }

  const StringPiece resource_name = ReadString(/*index=*/-1);
  StringPiece resource_content;
  if (!resources_.GetResourceContent(device_locale_, resource_name,
                                     &resource_content)) {
    // Resource cannot be provided by the model.
    return false;
  }

  PushString(resource_content);
  return true;
}

int JniLuaEnvironment::HandleAndroidStringResources() {
  // Check whether the requested resource can be served from the model data.
  if (LookupModelStringResource()) {
    return LUA_YIELD;
  }

  // Get system resources if not previously retrieved.
  if (!RetrieveSystemResources()) {
    TC3_LOG(ERROR) << "Error retrieving system resources.";
    lua_error(state_);
    return LUA_ERRRUN;
  }

  int resource_id;
  switch (lua_type(state_, -1)) {
    case LUA_TNUMBER:
      resource_id = static_cast<int>(lua_tonumber(state_, /*idx=*/-1));
      break;
    case LUA_TSTRING: {
      const StringPiece resource_name_str = ReadString(/*index=*/-1);
      if (resource_name_str.empty()) {
        TC3_LOG(ERROR) << "No resource name provided.";
        lua_error(state_);
        return LUA_ERRRUN;
      }
      ScopedLocalRef<jstring> resource_name =
          jni_cache_->ConvertToJavaString(resource_name_str);
      if (resource_name == nullptr) {
        TC3_LOG(ERROR) << "Invalid resource name.";
        lua_error(state_);
        return LUA_ERRRUN;
      }
      resource_id = jenv_->CallIntMethod(
          system_resources_.get(), jni_cache_->resources_get_identifier,
          resource_name.get(), string_.get(), android_.get());
      if (jni_cache_->ExceptionCheckAndClear()) {
        TC3_LOG(ERROR) << "Error calling getIdentifier.";
        lua_error(state_);
        return LUA_ERRRUN;
      }
      break;
    }
    default:
      TC3_LOG(ERROR) << "Unexpected type for resource lookup.";
      lua_error(state_);
      return LUA_ERRRUN;
  }
  if (resource_id == 0) {
    TC3_LOG(ERROR) << "Resource not found.";
    lua_pushnil(state_);
    return LUA_YIELD;
  }
  ScopedLocalRef<jstring> resource_str(static_cast<jstring>(
      jenv_->CallObjectMethod(system_resources_.get(),
                              jni_cache_->resources_get_string, resource_id)));
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling getString.";
    lua_error(state_);
    return LUA_ERRRUN;
  }
  if (resource_str == nullptr) {
    lua_pushnil(state_);
  } else {
    PushString(ToStlString(jenv_, resource_str.get()));
  }
  return LUA_YIELD;
}

bool JniLuaEnvironment::RetrieveSystemResources() {
  if (system_resources_resources_retrieved_) {
    return (system_resources_ != nullptr);
  }
  system_resources_resources_retrieved_ = true;
  jobject system_resources_ref = jenv_->CallStaticObjectMethod(
      jni_cache_->resources_class.get(), jni_cache_->resources_get_system);
  if (jni_cache_->ExceptionCheckAndClear()) {
    TC3_LOG(ERROR) << "Error calling getSystem.";
    return false;
  }
  system_resources_ =
      MakeGlobalRef(system_resources_ref, jenv_, jni_cache_->jvm);
  return (system_resources_ != nullptr);
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
    const StringPiece key = ReadString(/*index=*/-2);
    if (key.Equals("title")) {
      result.title = ReadString(/*index=*/-1).ToString();
    } else if (key.Equals("description")) {
      result.description = ReadString(/*index=*/-1).ToString();
    } else if (key.Equals("action")) {
      result.action = ReadString(/*index=*/-1).ToString();
    } else if (key.Equals("data")) {
      result.data = ReadString(/*index=*/-1).ToString();
    } else if (key.Equals("type")) {
      result.type = ReadString(/*index=*/-1).ToString();
    } else if (key.Equals("flags")) {
      result.flags = static_cast<int>(lua_tointeger(state_, /*idx=*/-1));
    } else if (key.Equals("package_name")) {
      result.package_name = ReadString(/*index=*/-1).ToString();
    } else if (key.Equals("request_code")) {
      result.request_code = static_cast<int>(lua_tointeger(state_, /*idx=*/-1));
    } else if (key.Equals("category")) {
      ReadCategories(&result.category);
    } else if (key.Equals("extra")) {
      ReadExtras(&result.extra);
    } else {
      TC3_LOG(INFO) << "Unknown entry: " << key.ToString();
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
    category->push_back(ReadString(/*index=*/-1).ToString());
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
      const StringPiece key = ReadString(/*index=*/-2);
      if (key.Equals("name")) {
        name = ReadString(/*index=*/-1).ToString();
      } else if (key.Equals("int_value")) {
        value = Variant(static_cast<int>(lua_tonumber(state_, /*idx=*/-1)));
      } else if (key.Equals("long_value")) {
        value = Variant(static_cast<int64>(lua_tonumber(state_, /*idx=*/-1)));
      } else if (key.Equals("float_value")) {
        value = Variant(static_cast<float>(lua_tonumber(state_, /*idx=*/-1)));
      } else if (key.Equals("bool_value")) {
        value = Variant(static_cast<bool>(lua_toboolean(state_, /*idx=*/-1)));
      } else if (key.Equals("string_value")) {
        value = Variant(ReadString(/*index=*/-1).ToString());
      } else {
        TC3_LOG(INFO) << "Unknown extra field: " << key.ToString();
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

// Actions intent generation.
std::unique_ptr<ActionsJniLuaEnvironment>
ActionsJniLuaEnvironment::CreateActionsJniLuaEnvironment(
    const Resources& resources, const JniCache* jni_cache,
    const jobject context, const Locale& device_locale,
    const Conversation& conversation, const ActionSuggestion& action,
    int64 reference_time_ms_utc) {
  if (jni_cache == nullptr) {
    return nullptr;
  }
  std::unique_ptr<ActionsJniLuaEnvironment> lua_env(
      new ActionsJniLuaEnvironment(resources, jni_cache, context, device_locale,
                                   conversation, action,
                                   reference_time_ms_utc));
  if (!lua_env->PrepareEnvironment()) {
    return nullptr;
  }
  return lua_env;
}

ActionsJniLuaEnvironment::ActionsJniLuaEnvironment(
    const Resources& resources, const JniCache* jni_cache,
    const jobject context, const Locale& device_locale,
    const Conversation& conversation, const ActionSuggestion& action,
    int64 reference_time_ms_utc)
    : JniLuaEnvironment(
          resources, jni_cache, context, device_locale, /*entity_text=*/"",
          conversation.messages.empty()
              ? 0
              : conversation.messages.back().reference_time_ms_utc,
          reference_time_ms_utc, action.extra),
      conversation_(conversation),
      action_(action) {}

void ActionsJniLuaEnvironment::SetupExternalHook() {
  JniLuaEnvironment::SetupExternalHook();

  // Extend the external object with hooks to access the annotations
  // associated with an action.
  PushString("annotation");
  SetupTableLookupCallback("annotation", CALLBACK_ID_ANNOTATION);
  lua_settable(state_, /*idx=*/-3);
}

int ActionsJniLuaEnvironment::HandleCallback(int callback_id,
                                             void* callback_arg) {
  switch (callback_id) {
    case CALLBACK_ID_ANNOTATION:
      return HandleAnnotationCallback();
    case CALLBACK_ID_ANNOTATION_EXTRAS:
      return HandleAnnotationExtrasCallback(callback_arg);
    default:
      return JniLuaEnvironment::HandleCallback(callback_id, callback_arg);
  }
}

int ActionsJniLuaEnvironment::HandleAnnotationCallback() {
  if (action_.annotations.empty()) {
    TC3_LOG(ERROR) << "No annotations provided.";
    lua_error(state_);
    return LUA_ERRRUN;
  }

  // We can directly access an annotation by index (lua is 1-based), or
  // by name.
  int64 annotation_id = -1;
  switch (lua_type(state_, -1)) {
    case LUA_TNUMBER:
      annotation_id = static_cast<int>(lua_tonumber(state_, /*idx=*/-1)) - 1;
      TC3_LOG(ERROR) << "Annotation lookup by id: " << annotation_id;
      break;
    case LUA_TSTRING: {
      const StringPiece annotation_name = ReadString(/*index=*/-1);

      TC3_LOG(ERROR) << "Annotation lookup: " << annotation_name.ToString();

      for (int i = 0; i < action_.annotations.size(); i++) {
        if (annotation_name.Equals(action_.annotations[i].name)) {
          annotation_id = i;
          break;
        }
      }
      if (annotation_id < 0) {
        TC3_LOG(ERROR) << "Annotation " << annotation_name.ToString()
                       << " not found.";
        lua_error(state_);
        return LUA_ERRRUN;
      }
      break;
    }
    default:
      TC3_LOG(ERROR) << "Unexpected type for annotation lookup.";
      lua_error(state_);
      return LUA_ERRRUN;
  }

  // Provide callback table for this annotation.
  lua_newtable(state_);
  PushString(action_.annotations[annotation_id].entity.collection);
  lua_setfield(state_, /*idx=*/-2, "collection");
  PushString("extras");
  SetupTableLookupCallback("extras", CALLBACK_ID_ANNOTATION_EXTRAS,
                           reinterpret_cast<void*>(annotation_id));
  lua_settable(state_, /*idx=*/-3);
  return LUA_YIELD;
}

int ActionsJniLuaEnvironment::HandleAnnotationExtrasCallback(
    void* callback_arg) {
  const int64 annotation_id = reinterpret_cast<int64>(callback_arg);
  if (annotation_id < 0 || annotation_id >= action_.annotations.size()) {
    TC3_LOG(ERROR) << "Invalid annotation lookup.";
    lua_error(state_);
    return LUA_ERRRUN;
  }
  const ActionSuggestionAnnotation& annotation =
      action_.annotations[annotation_id];
  const StringPiece key = ReadString(/*index=*/-1);
  if (key.Equals(kTextKey)) {
    if (annotation.message_index == kInvalidIndex) {
      lua_pushnil(state_);
      return LUA_YIELD;
    }
    // Extract text from message.
    if (annotation.message_index < 0 ||
        annotation.message_index >= conversation_.messages.size()) {
      TC3_LOG(ERROR) << "Invalid message reference.";
      lua_error(state_);
      return LUA_ERRRUN;
    }
    PushString(ExtractUtf8Substring(
        conversation_.messages[annotation.message_index].text,
        annotation.span));
  } else if (key.Equals(kTimeUsecKey)) {
    lua_pushinteger(state_,
                    annotation.entity.datetime_parse_result.time_ms_utc);
  } else {
    const auto it = annotation.entity.extra.find(key.ToString());
    if (it == extra_.end()) {
      TC3_LOG(ERROR) << "Undefined extra lookup " << key.ToString();
      lua_error(state_);
      return LUA_ERRRUN;
    }
    PushValue(it->second);
  }
  return LUA_YIELD;
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
  if (lua_gettop(state_) != 1 || lua_type(state_, /*idx=*/1) != LUA_TTABLE) {
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

std::unique_ptr<IntentGenerator> IntentGenerator::CreateIntentGenerator(
    const IntentFactoryModel* options, const ResourcePool* resources,
    const std::shared_ptr<JniCache>& jni_cache, const jobject context) {
  // Normally this check would be performed by the Java compiler and we wouldn't
  // need to worry about it here. But we can't depend on Android's SDK in Java,
  // so we check the instance type here.
  if (context != nullptr && !jni_cache->GetEnv()->IsInstanceOf(
                                context, jni_cache->context_class.get())) {
    TC3_LOG(ERROR) << "Provided context is not an android.content.Context";
    return nullptr;
  }

  return std::unique_ptr<IntentGenerator>(
      new IntentGenerator(options, resources, jni_cache, context));
}

IntentGenerator::IntentGenerator(const IntentFactoryModel* options,
                                 const ResourcePool* resources,
                                 const std::shared_ptr<JniCache>& jni_cache,
                                 const jobject context)
    : options_(options),
      resources_(Resources(resources)),
      jni_cache_(jni_cache),
      context_(context) {
  if (options == nullptr || options->generator() == nullptr) {
    return;
  }

  for (const IntentFactoryModel_::IntentGenerator* generator :
       *options_->generator()) {
    generators_[generator->type()->str()] =
        std::string(reinterpret_cast<const char*>(
                        generator->lua_template_generator()->data()),
                    generator->lua_template_generator()->size());
  }
}

Locale IntentGenerator::ParseLocale(const jstring device_locale) const {
  if (device_locale == nullptr) {
    TC3_LOG(ERROR) << "No locale provided.";
    return Locale::Invalid();
  }
  ScopedStringChars locale_str =
      GetScopedStringChars(jni_cache_->GetEnv(), device_locale);
  if (locale_str == nullptr) {
    TC3_LOG(ERROR) << "Cannot retrieve provided locale.";
    return Locale::Invalid();
  }
  return Locale::FromBCP47(reinterpret_cast<const char*>(locale_str.get()));
}

std::vector<RemoteActionTemplate> IntentGenerator::GenerateIntents(
    const jstring device_locale, const ClassificationResult& classification,
    int64 reference_time_ms_utc, const std::string& text,
    CodepointSpan selection_indices) const {
  if (options_ == nullptr) {
    return {};
  }

  // Retrieve generator for specified entity.
  auto it = generators_.find(classification.collection);
  if (it == generators_.end()) {
    TC3_LOG(INFO) << "Unknown entity: " << classification.collection;
    return {};
  }

  const std::string entity_text = ExtractUtf8Substring(text, selection_indices);

  std::unique_ptr<JniLuaEnvironment> interpreter =
      JniLuaEnvironment::CreateJniLuaEnvironment(
          resources_, jni_cache_.get(), context_, ParseLocale(device_locale),
          entity_text, classification.datetime_parse_result.time_ms_utc,
          reference_time_ms_utc, classification.extra);

  if (interpreter == nullptr) {
    TC3_LOG(ERROR) << "Could not create Lua interpreter.";
    return {};
  }

  return interpreter->RunIntentGenerator(it->second);
}

std::vector<RemoteActionTemplate> IntentGenerator::GenerateIntents(
    const jstring device_locale, const ActionSuggestion& action,
    int64 reference_time_ms_utc, const Conversation& conversation) const {
  if (options_ == nullptr) {
    return {};
  }

  // Retrieve generator for specified action.
  auto it = generators_.find(action.type);
  if (it == generators_.end()) {
    TC3_LOG(INFO) << "No generator for action type: " << action.type;
    return {};
  }

  std::unique_ptr<ActionsJniLuaEnvironment> interpreter =
      ActionsJniLuaEnvironment::CreateActionsJniLuaEnvironment(
          resources_, jni_cache_.get(), context_, ParseLocale(device_locale),
          conversation, action, reference_time_ms_utc);

  if (interpreter == nullptr) {
    TC3_LOG(ERROR) << "Could not create Lua interpreter.";
    return {};
  }

  return interpreter->RunIntentGenerator(it->second);
}

}  // namespace libtextclassifier3
