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

#include "utils/intents/jni.h"
#include <memory>
#include "utils/intents/intent-generator.h"
#include "utils/java/scoped_local_ref.h"

namespace libtextclassifier3 {

// The macros below are intended to reduce the boilerplate and avoid
// easily introduced copy/paste errors.
#define TC3_CHECK_JNI_PTR(PTR) TC3_CHECK((PTR) != nullptr)
#define TC3_GET_CLASS(FIELD, NAME)            \
  handler->FIELD.reset(env->FindClass(NAME)); \
  TC3_CHECK_JNI_PTR(handler->FIELD) << "Error finding class: " << NAME;
#define TC3_GET_METHOD(CLASS, FIELD, NAME, SIGNATURE)                       \
  handler->FIELD = env->GetMethodID(handler->CLASS.get(), NAME, SIGNATURE); \
  TC3_CHECK(handler->FIELD) << "Error finding method: " << NAME;

std::unique_ptr<RemoteActionTemplatesHandler>
RemoteActionTemplatesHandler::Create(JNIEnv* env) {
  if (env == nullptr) {
    return nullptr;
  }
  std::unique_ptr<RemoteActionTemplatesHandler> handler(
      new RemoteActionTemplatesHandler(env));

  TC3_GET_CLASS(string_class_, "java/lang/String");
  TC3_GET_CLASS(integer_class_, "java/lang/Integer");
  TC3_GET_METHOD(integer_class_, integer_init_, "<init>", "(I)V");

  TC3_GET_CLASS(remote_action_template_class_,
                TC3_PACKAGE_PATH TC3_REMOTE_ACTION_TEMPLATE_CLASS_NAME_STR);
  TC3_GET_METHOD(
      remote_action_template_class_, remote_action_template_init_, "<init>",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
      "String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/Integer;[Ljava/"
      "lang/String;Ljava/lang/String;[L" TC3_PACKAGE_PATH
          TC3_NAMED_VARIANT_CLASS_NAME_STR ";Ljava/lang/Integer;)V");

  TC3_GET_CLASS(named_variant_class_,
                TC3_PACKAGE_PATH TC3_NAMED_VARIANT_CLASS_NAME_STR);

  TC3_GET_METHOD(named_variant_class_, named_variant_from_int_, "<init>",
                 "(Ljava/lang/String;I)V");
  TC3_GET_METHOD(named_variant_class_, named_variant_from_long_, "<init>",
                 "(Ljava/lang/String;J)V");
  TC3_GET_METHOD(named_variant_class_, named_variant_from_float_, "<init>",
                 "(Ljava/lang/String;F)V");
  TC3_GET_METHOD(named_variant_class_, named_variant_from_double_, "<init>",
                 "(Ljava/lang/String;D)V");
  TC3_GET_METHOD(named_variant_class_, named_variant_from_bool_, "<init>",
                 "(Ljava/lang/String;Z)V");
  TC3_GET_METHOD(named_variant_class_, named_variant_from_string_, "<init>",
                 "(Ljava/lang/String;Ljava/lang/String;)V");

  return handler;
}

jstring RemoteActionTemplatesHandler::AsUTF8String(
    const Optional<std::string>& optional) {
  return (optional.has_value() ? env_->NewStringUTF(optional.value().c_str())
                               : nullptr);
}

jobject RemoteActionTemplatesHandler::AsInteger(const Optional<int>& optional) {
  return (optional.has_value()
              ? env_->NewObject(integer_class_.get(), integer_init_,
                                optional.value())
              : nullptr);
}

jobjectArray RemoteActionTemplatesHandler::AsStringArray(
    const std::vector<std::string>& values) {
  if (values.empty()) {
    return nullptr;
  }
  jobjectArray result =
      env_->NewObjectArray(values.size(), string_class_.get(), nullptr);
  if (result == nullptr) {
    return nullptr;
  }
  for (int k = 0; k < values.size(); k++) {
    env_->SetObjectArrayElement(result, k,
                                env_->NewStringUTF(values[k].c_str()));
  }
  return result;
}

jobject RemoteActionTemplatesHandler::AsNamedVariant(const std::string& name,
                                                     const Variant& value) {
  jstring jname = env_->NewStringUTF(name.c_str());
  if (jname == nullptr) {
    return nullptr;
  }
  switch (value.GetType()) {
    case VariantValue_::Type_INT_VALUE:
      return env_->NewObject(named_variant_class_.get(),
                             named_variant_from_int_, jname, value.IntValue());
    case VariantValue_::Type_INT64_VALUE:
      return env_->NewObject(named_variant_class_.get(),
                             named_variant_from_long_, jname,
                             value.Int64Value());
    case VariantValue_::Type_FLOAT_VALUE:
      return env_->NewObject(named_variant_class_.get(),
                             named_variant_from_float_, jname,
                             value.FloatValue());
    case VariantValue_::Type_DOUBLE_VALUE:
      return env_->NewObject(named_variant_class_.get(),
                             named_variant_from_double_, jname,
                             value.DoubleValue());
    case VariantValue_::Type_BOOL_VALUE:
      return env_->NewObject(named_variant_class_.get(),
                             named_variant_from_bool_, jname,
                             value.BoolValue());
    case VariantValue_::Type_STRING_VALUE: {
      jstring jstring = env_->NewStringUTF(value.StringValue().c_str());
      if (jstring == nullptr) {
        return nullptr;
      }
      return env_->NewObject(named_variant_class_.get(),
                             named_variant_from_string_, jname, jstring);
    }
    default:
      return nullptr;
  }
}

jobjectArray RemoteActionTemplatesHandler::AsNamedVariantArray(
    const std::map<std::string, Variant>& values) {
  if (values.empty()) {
    return nullptr;
  }
  jobjectArray result =
      env_->NewObjectArray(values.size(), named_variant_class_.get(), nullptr);
  int element_index = 0;
  for (auto key_value_pair : values) {
    if (!key_value_pair.second.HasValue()) {
      element_index++;
      continue;
    }
    ScopedLocalRef<jobject> named_extra(
        AsNamedVariant(key_value_pair.first, key_value_pair.second), env_);
    if (named_extra == nullptr) {
      return nullptr;
    }
    env_->SetObjectArrayElement(result, element_index, named_extra.get());
    element_index++;
  }
  return result;
}

jobjectArray RemoteActionTemplatesHandler::RemoteActionTemplatesToJObjectArray(
    const std::vector<RemoteActionTemplate>& remote_actions) {
  const jobjectArray results = env_->NewObjectArray(
      remote_actions.size(), remote_action_template_class_.get(), nullptr);
  if (results == nullptr) {
    return nullptr;
  }
  for (int i = 0; i < remote_actions.size(); i++) {
    const RemoteActionTemplate& remote_action = remote_actions[i];
    const jstring title = AsUTF8String(remote_action.title);
    const jstring description = AsUTF8String(remote_action.description);
    const jstring action = AsUTF8String(remote_action.action);
    const jstring data = AsUTF8String(remote_action.data);
    const jstring type = AsUTF8String(remote_action.type);
    const jobject flags = AsInteger(remote_action.flags);
    const jobjectArray category = AsStringArray(remote_action.category);
    const jstring package = AsUTF8String(remote_action.package_name);
    const jobjectArray extra = AsNamedVariantArray(remote_action.extra);
    const jobject request_code = AsInteger(remote_action.request_code);
    ScopedLocalRef<jobject> result(
        env_->NewObject(remote_action_template_class_.get(),
                        remote_action_template_init_, title, description,
                        action, data, type, flags, category, package, extra,
                        request_code),
        env_);
    if (result == nullptr) {
      return nullptr;
    }
    env_->SetObjectArrayElement(results, i, result.get());
  }
  return results;
}

}  // namespace libtextclassifier3
