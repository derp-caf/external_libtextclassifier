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

#include "annotator/annotator_jni_common.h"

#include "utils/java/jni-base.h"
#include "utils/java/scoped_local_ref.h"

namespace libtextclassifier3 {
namespace {
template <typename T>
T FromJavaOptionsInternal(JNIEnv* env, jobject joptions,
                          const std::string& class_name) {
  if (!joptions) {
    return {};
  }

  const ScopedLocalRef<jclass> options_class(env->FindClass(class_name.c_str()),
                                             env);
  if (!options_class) {
    return {};
  }

  const std::pair<bool, jobject> status_or_locales = CallJniMethod0<jobject>(
      env, joptions, options_class.get(), &JNIEnv::CallObjectMethod,
      "getLocale", "Ljava/lang/String;");
  const std::pair<bool, jobject> status_or_reference_timezone =
      CallJniMethod0<jobject>(env, joptions, options_class.get(),
                              &JNIEnv::CallObjectMethod, "getReferenceTimezone",
                              "Ljava/lang/String;");
  const std::pair<bool, int64> status_or_reference_time_ms_utc =
      CallJniMethod0<int64>(env, joptions, options_class.get(),
                            &JNIEnv::CallLongMethod, "getReferenceTimeMsUtc",
                            "J");

  if (!status_or_locales.first || !status_or_reference_timezone.first ||
      !status_or_reference_time_ms_utc.first) {
    return {};
  }

  T options;
  options.locales =
      ToStlString(env, reinterpret_cast<jstring>(status_or_locales.second));
  options.reference_timezone = ToStlString(
      env, reinterpret_cast<jstring>(status_or_reference_timezone.second));
  options.reference_time_ms_utc = status_or_reference_time_ms_utc.second;
  return options;
}
}  // namespace

SelectionOptions FromJavaSelectionOptions(JNIEnv* env, jobject joptions) {
  if (!joptions) {
    return {};
  }

  const ScopedLocalRef<jclass> options_class(
      env->FindClass(TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR
                     "$SelectionOptions"),
      env);
  const std::pair<bool, jobject> status_or_locales = CallJniMethod0<jobject>(
      env, joptions, options_class.get(), &JNIEnv::CallObjectMethod,
      "getLocales", "Ljava/lang/String;");
  if (!status_or_locales.first) {
    return {};
  }

  SelectionOptions options;
  options.locales =
      ToStlString(env, reinterpret_cast<jstring>(status_or_locales.second));

  return options;
}

ClassificationOptions FromJavaClassificationOptions(JNIEnv* env,
                                                    jobject joptions) {
  return FromJavaOptionsInternal<ClassificationOptions>(
      env, joptions,
      TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR "$ClassificationOptions");
}

AnnotationOptions FromJavaAnnotationOptions(JNIEnv* env, jobject joptions) {
  return FromJavaOptionsInternal<AnnotationOptions>(
      env, joptions,
      TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR "$AnnotationOptions");
}

}  // namespace libtextclassifier3
