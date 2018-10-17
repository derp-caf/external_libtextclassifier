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

// JNI wrapper for actions.

#include "actions/actions_jni.h"

#include <jni.h>
#include <type_traits>
#include <vector>

#include "actions/actions-suggestions.h"
#include "utils/base/integral_types.h"
#include "utils/java/scoped_local_ref.h"

using libtextclassifier3::ActionsSuggestions;
using libtextclassifier3::ActionSuggestion;
using libtextclassifier3::ActionSuggestionOptions;
using libtextclassifier3::Conversation;
using libtextclassifier3::ScopedLocalRef;
using libtextclassifier3::ToStlString;

namespace libtextclassifier3 {

namespace {
ActionSuggestionOptions FromJavaActionSuggestionOptions(JNIEnv* env,
                                                        jobject joptions) {
  return {};
}

jobjectArray ActionSuggestionsToJObjectArray(
    JNIEnv* env, const std::vector<ActionSuggestion>& action_result) {
  const ScopedLocalRef<jclass> result_class(
      env->FindClass(TC3_PACKAGE_PATH TC3_ACTIONS_CLASS_NAME_STR
                     "$ActionSuggestion"),
      env);
  if (!result_class) {
    TC3_LOG(ERROR) << "Couldn't find ActionSuggestion class.";
    return nullptr;
  }

  const jmethodID result_class_constructor = env->GetMethodID(
      result_class.get(), "<init>", "(Ljava/lang/String;Ljava/lang/String;F)V");
  const jobjectArray results =
      env->NewObjectArray(action_result.size(), result_class.get(), nullptr);
  for (int i = 0; i < action_result.size(); i++) {
    ScopedLocalRef<jobject> result(env->NewObject(
        result_class.get(), result_class_constructor,
        env->NewStringUTF(action_result[i].response_text.c_str()),
        env->NewStringUTF(action_result[i].type.c_str()),
        static_cast<jfloat>(action_result[i].score)));
    env->SetObjectArrayElement(results, i, result.get());
  }
  return results;
}

ConversationMessage FromJavaConversationMessage(JNIEnv* env, jobject jmessage) {
  if (!jmessage) {
    return {};
  }

  const ScopedLocalRef<jclass> message_class(
      env->FindClass(TC3_PACKAGE_PATH TC3_ACTIONS_CLASS_NAME_STR
                     "$ConversationMessage"),
      env);
  const std::pair<bool, jobject> status_or_text = CallJniMethod0<jobject>(
      env, jmessage, message_class.get(), &JNIEnv::CallObjectMethod, "getText",
      "Ljava/lang/String;");
  const std::pair<bool, int32> status_or_user_id =
      CallJniMethod0<int32>(env, jmessage, message_class.get(),
                            &JNIEnv::CallIntMethod, "getUserId", "I");
  if (!status_or_text.first || !status_or_user_id.first) {
    return {};
  }

  ConversationMessage message;
  message.text =
      ToStlString(env, reinterpret_cast<jstring>(status_or_text.second));
  message.user_id = status_or_user_id.second;
  return message;
}

Conversation FromJavaConversation(JNIEnv* env, jobject jconversation) {
  if (!jconversation) {
    return {};
  }

  const ScopedLocalRef<jclass> conversation_class(
      env->FindClass(TC3_PACKAGE_PATH TC3_ACTIONS_CLASS_NAME_STR
                     "$Conversation"),
      env);

  const std::pair<bool, jobject> status_or_messages = CallJniMethod0<jobject>(
      env, jconversation, conversation_class.get(), &JNIEnv::CallObjectMethod,
      "getConversationMessages",
      "[L" TC3_PACKAGE_PATH TC3_ACTIONS_CLASS_NAME_STR "$ConversationMessage;");

  if (!status_or_messages.first) {
    return {};
  }

  const jobjectArray jmessages =
      reinterpret_cast<jobjectArray>(status_or_messages.second);

  const int size = env->GetArrayLength(jmessages);

  std::vector<ConversationMessage> messages;
  for (int i = 0; i < size; i++) {
    jobject jmessage = env->GetObjectArrayElement(jmessages, i);
    ConversationMessage message = FromJavaConversationMessage(env, jmessage);
    messages.push_back(message);
  }
  Conversation conversation;
  conversation.messages = messages;
  return conversation;
}
}  // namespace
}  // namespace libtextclassifier3

using libtextclassifier3::ActionSuggestionsToJObjectArray;
using libtextclassifier3::FromJavaActionSuggestionOptions;
using libtextclassifier3::FromJavaConversation;

TC3_JNI_METHOD(jlong, TC3_ACTIONS_CLASS_NAME, nativeNewActionsModel)
(JNIEnv* env, jobject thiz, jint fd) {
  return reinterpret_cast<jlong>(
      ActionsSuggestions::FromFileDescriptor(fd).release());
}

TC3_JNI_METHOD(jlong, TC3_ACTIONS_CLASS_NAME, nativeNewActionsModelFromPath)
(JNIEnv* env, jobject thiz, jstring path) {
  const std::string path_str = ToStlString(env, path);
  return reinterpret_cast<jlong>(
      ActionsSuggestions::FromPath(path_str).release());
}

TC3_JNI_METHOD(jlong, TC3_ACTIONS_CLASS_NAME,
               nativeNewActionModelsFromAssetFileDescriptor)
(JNIEnv* env, jobject thiz, jobject afd, jlong offset, jlong size) {
  const jint fd = libtextclassifier3::GetFdFromAssetFileDescriptor(env, afd);
  return reinterpret_cast<jlong>(
      ActionsSuggestions::FromFileDescriptor(fd, offset, size).release());
}

TC3_JNI_METHOD(jobjectArray, TC3_ACTIONS_CLASS_NAME, nativeSuggestActions)
(JNIEnv* env, jobject thiz, jlong ptr, jobject jconversation,
 jobject joptions) {
  if (!ptr) {
    return nullptr;
  }
  const Conversation conversation = FromJavaConversation(env, jconversation);
  const ActionSuggestionOptions actionSuggestionOptions =
      FromJavaActionSuggestionOptions(env, joptions);
  ActionsSuggestions* action_model = reinterpret_cast<ActionsSuggestions*>(ptr);

  const std::vector<ActionSuggestion> action_suggestions =
      action_model->SuggestActions(conversation, actionSuggestionOptions);
  return ActionSuggestionsToJObjectArray(env, action_suggestions);
}

TC3_JNI_METHOD(void, TC3_ACTIONS_CLASS_NAME, nativeCloseActionsModel)
(JNIEnv* env, jobject thiz, jlong ptr) {
  ActionsSuggestions* model = reinterpret_cast<ActionsSuggestions*>(ptr);
  delete model;
}
