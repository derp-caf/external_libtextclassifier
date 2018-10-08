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

#ifndef LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_JNI_H_
#define LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_JNI_H_

#include <jni.h>
#include <string>
#include "utils/java/jni-base.h"

#ifndef TC3_ACTIONS_CLASS_NAME
#define TC3_ACTIONS_CLASS_NAME ActionsSuggestionsModel
#endif

#define TC3_ACTIONS_CLASS_NAME_STR TC3_ADD_QUOTES(TC3_ACTIONS_CLASS_NAME)

#ifdef __cplusplus
extern "C" {
#endif

TC3_JNI_METHOD(jlong, TC3_ACTIONS_CLASS_NAME, nativeNewActionsModel)
(JNIEnv* env, jobject thiz, jint fd);

TC3_JNI_METHOD(jlong, TC3_ACTIONS_CLASS_NAME, nativeNewActionsModelFromPath)
(JNIEnv* env, jobject thiz, jstring path);

TC3_JNI_METHOD(jlong, TC3_ACTIONS_CLASS_NAME,
               nativeNewActionsModelModelsFromAssetFileDescriptor)
(JNIEnv* env, jobject thiz, jobject afd, jlong offset, jlong size);

TC3_JNI_METHOD(jobjectArray, TC3_ACTIONS_CLASS_NAME, nativeSuggestActions)
(JNIEnv* env, jobject thiz, jlong ptr, jobject jconversation, jobject joptions);

TC3_JNI_METHOD(void, TC3_ACTIONS_CLASS_NAME, nativeCloseActionsModel)
(JNIEnv* env, jobject thiz, jlong ptr);

#ifdef __cplusplus
}
#endif

#endif  // LIBTEXTCLASSIFIER_ACTIONS_ACTIONS_JNI_H_
