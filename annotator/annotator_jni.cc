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

// JNI wrapper for the Annotator.

#include "annotator/annotator_jni.h"

#include <jni.h>
#include <type_traits>
#include <vector>

#include "annotator/annotator.h"
#include "utils/base/integral_types.h"
#include "utils/calendar/calendar.h"
#include "utils/java/scoped_local_ref.h"
#include "utils/java/string_utils.h"
#include "utils/memory/mmap.h"
#include "utils/utf8/unilib.h"

#ifdef LIBTEXTCLASSIFIER_UNILIB_JAVAICU
#ifndef LIBTEXTCLASSIFIER_CALENDAR_JAVAICU
#error Inconsistent usage of Java ICU components
#else
#define TC3_USE_JAVAICU
#endif
#endif

using libtextclassifier3::AnnotatedSpan;
using libtextclassifier3::AnnotationOptions;
using libtextclassifier3::Annotator;
using libtextclassifier3::ClassificationOptions;
using libtextclassifier3::ClassificationResult;
using libtextclassifier3::CodepointSpan;
using libtextclassifier3::JStringToUtf8String;
using libtextclassifier3::Model;
using libtextclassifier3::ScopedLocalRef;
using libtextclassifier3::SelectionOptions;
// When using the Java's ICU, CalendarLib and UniLib need to be instantiated
// with a JavaVM pointer from JNI. When using a standard ICU the pointer is
// not needed and the objects are instantiated implicitly.
#ifdef TC3_USE_JAVAICU
using libtextclassifier3::CalendarLib;
using libtextclassifier3::UniLib;
#endif

namespace libtextclassifier3 {

using libtextclassifier3::CodepointSpan;

namespace {

jobjectArray ClassificationResultsToJObjectArray(
    JNIEnv* env,
    const std::vector<ClassificationResult>& classification_result) {
  const ScopedLocalRef<jclass> result_class(
      env->FindClass(TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR
                     "$ClassificationResult"),
      env);
  if (!result_class) {
    TC3_LOG(ERROR) << "Couldn't find ClassificationResult class.";
    return nullptr;
  }
  const ScopedLocalRef<jclass> datetime_parse_class(
      env->FindClass(TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR
                     "$DatetimeResult"),
      env);
  if (!datetime_parse_class) {
    TC3_LOG(ERROR) << "Couldn't find DatetimeResult class.";
    return nullptr;
  }

  const jmethodID result_class_constructor = env->GetMethodID(
      result_class.get(), "<init>",
      "(Ljava/lang/String;FL" TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR
      "$DatetimeResult;[B)V");
  const jmethodID datetime_parse_class_constructor =
      env->GetMethodID(datetime_parse_class.get(), "<init>", "(JI)V");

  const jobjectArray results = env->NewObjectArray(classification_result.size(),
                                                   result_class.get(), nullptr);
  for (int i = 0; i < classification_result.size(); i++) {
    jstring row_string =
        env->NewStringUTF(classification_result[i].collection.c_str());

    jobject row_datetime_parse = nullptr;
    if (classification_result[i].datetime_parse_result.IsSet()) {
      row_datetime_parse = env->NewObject(
          datetime_parse_class.get(), datetime_parse_class_constructor,
          classification_result[i].datetime_parse_result.time_ms_utc,
          classification_result[i].datetime_parse_result.granularity);
    }

    jbyteArray serialized_knowledge_result = nullptr;
    const std::string& serialized_knowledge_result_string =
        classification_result[i].serialized_knowledge_result;
    if (!serialized_knowledge_result_string.empty()) {
      serialized_knowledge_result =
          env->NewByteArray(serialized_knowledge_result_string.size());
      env->SetByteArrayRegion(serialized_knowledge_result, 0,
                              serialized_knowledge_result_string.size(),
                              reinterpret_cast<const jbyte*>(
                                  serialized_knowledge_result_string.data()));
    }

    jobject result =
        env->NewObject(result_class.get(), result_class_constructor, row_string,
                       static_cast<jfloat>(classification_result[i].score),
                       row_datetime_parse, serialized_knowledge_result);
    env->SetObjectArrayElement(results, i, result);
    env->DeleteLocalRef(result);
  }
  return results;
}

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

CodepointSpan ConvertIndicesBMPUTF8(const std::string& utf8_str,
                                    CodepointSpan orig_indices,
                                    bool from_utf8) {
  const libtextclassifier3::UnicodeText unicode_str =
      libtextclassifier3::UTF8ToUnicodeText(utf8_str, /*do_copy=*/false);

  int unicode_index = 0;
  int bmp_index = 0;

  const int* source_index;
  const int* target_index;
  if (from_utf8) {
    source_index = &unicode_index;
    target_index = &bmp_index;
  } else {
    source_index = &bmp_index;
    target_index = &unicode_index;
  }

  CodepointSpan result{-1, -1};
  std::function<void()> assign_indices_fn = [&result, &orig_indices,
                                             &source_index, &target_index]() {
    if (orig_indices.first == *source_index) {
      result.first = *target_index;
    }

    if (orig_indices.second == *source_index) {
      result.second = *target_index;
    }
  };

  for (auto it = unicode_str.begin(); it != unicode_str.end();
       ++it, ++unicode_index, ++bmp_index) {
    assign_indices_fn();

    // There is 1 extra character in the input for each UTF8 character > 0xFFFF.
    if (*it > 0xFFFF) {
      ++bmp_index;
    }
  }
  assign_indices_fn();

  return result;
}

}  // namespace

CodepointSpan ConvertIndicesBMPToUTF8(const std::string& utf8_str,
                                      CodepointSpan bmp_indices) {
  return ConvertIndicesBMPUTF8(utf8_str, bmp_indices, /*from_utf8=*/false);
}

CodepointSpan ConvertIndicesUTF8ToBMP(const std::string& utf8_str,
                                      CodepointSpan utf8_indices) {
  return ConvertIndicesBMPUTF8(utf8_str, utf8_indices, /*from_utf8=*/true);
}

jstring GetLocalesFromMmap(JNIEnv* env, libtextclassifier3::ScopedMmap* mmap) {
  if (!mmap->handle().ok()) {
    return env->NewStringUTF("");
  }
  const Model* model = libtextclassifier3::ViewModel(
      mmap->handle().start(), mmap->handle().num_bytes());
  if (!model || !model->locales()) {
    return env->NewStringUTF("");
  }
  return env->NewStringUTF(model->locales()->c_str());
}

jint GetVersionFromMmap(JNIEnv* env, libtextclassifier3::ScopedMmap* mmap) {
  if (!mmap->handle().ok()) {
    return 0;
  }
  const Model* model = libtextclassifier3::ViewModel(
      mmap->handle().start(), mmap->handle().num_bytes());
  if (!model) {
    return 0;
  }
  return model->version();
}

jstring GetNameFromMmap(JNIEnv* env, libtextclassifier3::ScopedMmap* mmap) {
  if (!mmap->handle().ok()) {
    return env->NewStringUTF("");
  }
  const Model* model = libtextclassifier3::ViewModel(
      mmap->handle().start(), mmap->handle().num_bytes());
  if (!model || !model->name()) {
    return env->NewStringUTF("");
  }
  return env->NewStringUTF(model->name()->c_str());
}

}  // namespace libtextclassifier3

using libtextclassifier3::ClassificationResultsToJObjectArray;
using libtextclassifier3::ConvertIndicesBMPToUTF8;
using libtextclassifier3::ConvertIndicesUTF8ToBMP;
using libtextclassifier3::FromJavaAnnotationOptions;
using libtextclassifier3::FromJavaClassificationOptions;
using libtextclassifier3::FromJavaSelectionOptions;
using libtextclassifier3::ToStlString;

TC3_JNI_METHOD(jlong, TC3_ANNOTATOR_CLASS_NAME, nativeNewAnnotator)
(JNIEnv* env, jobject thiz, jint fd) {
#ifdef TC3_USE_JAVAICU
  std::shared_ptr<libtextclassifier3::JniCache> jni_cache(
      libtextclassifier3::JniCache::Create(env));
  return reinterpret_cast<jlong>(Annotator::FromFileDescriptor(fd).release(),
                                 new UniLib(jni_cache),
                                 new CalendarLib(jni_cache));
#else
  return reinterpret_cast<jlong>(Annotator::FromFileDescriptor(fd).release());
#endif
}

TC3_JNI_METHOD(jlong, TC3_ANNOTATOR_CLASS_NAME, nativeNewAnnotatorFromPath)
(JNIEnv* env, jobject thiz, jstring path) {
  const std::string path_str = ToStlString(env, path);
#ifdef TC3_USE_JAVAICU
  std::shared_ptr<libtextclassifier3::JniCache> jni_cache(
      libtextclassifier3::JniCache::Create(env));
  return reinterpret_cast<jlong>(Annotator::FromPath(path_str,
                                                     new UniLib(jni_cache),
                                                     new CalendarLib(jni_cache))
                                     .release());
#else
  return reinterpret_cast<jlong>(Annotator::FromPath(path_str).release());
#endif
}

TC3_JNI_METHOD(jlong, TC3_ANNOTATOR_CLASS_NAME,
               nativeNewAnnotatorFromAssetFileDescriptor)
(JNIEnv* env, jobject thiz, jobject afd, jlong offset, jlong size) {
  const jint fd = libtextclassifier3::GetFdFromAssetFileDescriptor(env, afd);
#ifdef TC3_USE_JAVAICU
  std::shared_ptr<libtextclassifier3::JniCache> jni_cache(
      libtextclassifier3::JniCache::Create(env));
  return reinterpret_cast<jlong>(
      Annotator::FromFileDescriptor(fd, offset, size, new UniLib(jni_cache),
                                    new CalendarLib(jni_cache))
          .release());
#else
  return reinterpret_cast<jlong>(
      Annotator::FromFileDescriptor(fd, offset, size).release());
#endif
}

TC3_JNI_METHOD(jboolean, TC3_ANNOTATOR_CLASS_NAME,
               nativeInitializeKnowledgeEngine)
(JNIEnv* env, jobject thiz, jlong ptr, jbyteArray serialized_config) {
  if (!ptr) {
    return false;
  }

  Annotator* model = reinterpret_cast<Annotator*>(ptr);

  std::string serialized_config_string;
  const int length = env->GetArrayLength(serialized_config);
  serialized_config_string.resize(length);
  env->GetByteArrayRegion(serialized_config, 0, length,
                          reinterpret_cast<jbyte*>(const_cast<char*>(
                              serialized_config_string.data())));

  return model->InitializeKnowledgeEngine(serialized_config_string);
}

TC3_JNI_METHOD(jintArray, TC3_ANNOTATOR_CLASS_NAME, nativeSuggestSelection)
(JNIEnv* env, jobject thiz, jlong ptr, jstring context, jint selection_begin,
 jint selection_end, jobject options) {
  if (!ptr) {
    return nullptr;
  }

  Annotator* model = reinterpret_cast<Annotator*>(ptr);

  const std::string context_utf8 = ToStlString(env, context);
  CodepointSpan input_indices =
      ConvertIndicesBMPToUTF8(context_utf8, {selection_begin, selection_end});
  CodepointSpan selection = model->SuggestSelection(
      context_utf8, input_indices, FromJavaSelectionOptions(env, options));
  selection = ConvertIndicesUTF8ToBMP(context_utf8, selection);

  jintArray result = env->NewIntArray(2);
  env->SetIntArrayRegion(result, 0, 1, &(std::get<0>(selection)));
  env->SetIntArrayRegion(result, 1, 1, &(std::get<1>(selection)));
  return result;
}

TC3_JNI_METHOD(jobjectArray, TC3_ANNOTATOR_CLASS_NAME, nativeClassifyText)
(JNIEnv* env, jobject thiz, jlong ptr, jstring context, jint selection_begin,
 jint selection_end, jobject options) {
  if (!ptr) {
    return nullptr;
  }
  Annotator* ff_model = reinterpret_cast<Annotator*>(ptr);

  const std::string context_utf8 = ToStlString(env, context);
  const CodepointSpan input_indices =
      ConvertIndicesBMPToUTF8(context_utf8, {selection_begin, selection_end});
  const std::vector<ClassificationResult> classification_result =
      ff_model->ClassifyText(context_utf8, input_indices,
                             FromJavaClassificationOptions(env, options));

  return ClassificationResultsToJObjectArray(env, classification_result);
}

TC3_JNI_METHOD(jobjectArray, TC3_ANNOTATOR_CLASS_NAME, nativeAnnotate)
(JNIEnv* env, jobject thiz, jlong ptr, jstring context, jobject options) {
  if (!ptr) {
    return nullptr;
  }
  Annotator* model = reinterpret_cast<Annotator*>(ptr);
  std::string context_utf8 = ToStlString(env, context);
  std::vector<AnnotatedSpan> annotations =
      model->Annotate(context_utf8, FromJavaAnnotationOptions(env, options));

  jclass result_class = env->FindClass(
      TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR "$AnnotatedSpan");
  if (!result_class) {
    TC3_LOG(ERROR) << "Couldn't find result class: "
                   << TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR
        "$AnnotatedSpan";
    return nullptr;
  }

  jmethodID result_class_constructor =
      env->GetMethodID(result_class, "<init>",
                       "(II[L" TC3_PACKAGE_PATH TC3_ANNOTATOR_CLASS_NAME_STR
                       "$ClassificationResult;)V");

  jobjectArray results =
      env->NewObjectArray(annotations.size(), result_class, nullptr);

  for (int i = 0; i < annotations.size(); ++i) {
    CodepointSpan span_bmp =
        ConvertIndicesUTF8ToBMP(context_utf8, annotations[i].span);
    jobject result = env->NewObject(result_class, result_class_constructor,
                                    static_cast<jint>(span_bmp.first),
                                    static_cast<jint>(span_bmp.second),
                                    ClassificationResultsToJObjectArray(
                                        env, annotations[i].classification));
    env->SetObjectArrayElement(results, i, result);
    env->DeleteLocalRef(result);
  }
  env->DeleteLocalRef(result_class);
  return results;
}

TC3_JNI_METHOD(void, TC3_ANNOTATOR_CLASS_NAME, nativeCloseAnnotator)
(JNIEnv* env, jobject thiz, jlong ptr) {
  Annotator* model = reinterpret_cast<Annotator*>(ptr);
  delete model;
}

TC3_JNI_METHOD(jstring, TC3_ANNOTATOR_CLASS_NAME, nativeGetLanguage)
(JNIEnv* env, jobject clazz, jint fd) {
  TC3_LOG(WARNING) << "Using deprecated getLanguage().";
  return TC3_JNI_METHOD_NAME(TC3_ANNOTATOR_CLASS_NAME, nativeGetLocales)(
      env, clazz, fd);
}

TC3_JNI_METHOD(jstring, TC3_ANNOTATOR_CLASS_NAME, nativeGetLocales)
(JNIEnv* env, jobject clazz, jint fd) {
  const std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd));
  return GetLocalesFromMmap(env, mmap.get());
}

TC3_JNI_METHOD(jstring, TC3_ANNOTATOR_CLASS_NAME,
               nativeGetLocalesFromAssetFileDescriptor)
(JNIEnv* env, jobject thiz, jobject afd, jlong offset, jlong size) {
  const jint fd = libtextclassifier3::GetFdFromAssetFileDescriptor(env, afd);
  const std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd, offset, size));
  return GetLocalesFromMmap(env, mmap.get());
}

TC3_JNI_METHOD(jint, TC3_ANNOTATOR_CLASS_NAME, nativeGetVersion)
(JNIEnv* env, jobject clazz, jint fd) {
  const std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd));
  return GetVersionFromMmap(env, mmap.get());
}

TC3_JNI_METHOD(jint, TC3_ANNOTATOR_CLASS_NAME,
               nativeGetVersionFromAssetFileDescriptor)
(JNIEnv* env, jobject thiz, jobject afd, jlong offset, jlong size) {
  const jint fd = libtextclassifier3::GetFdFromAssetFileDescriptor(env, afd);
  const std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd, offset, size));
  return GetVersionFromMmap(env, mmap.get());
}

TC3_JNI_METHOD(jstring, TC3_ANNOTATOR_CLASS_NAME, nativeGetName)
(JNIEnv* env, jobject clazz, jint fd) {
  const std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd));
  return GetNameFromMmap(env, mmap.get());
}

TC3_JNI_METHOD(jstring, TC3_ANNOTATOR_CLASS_NAME,
               nativeGetNameFromAssetFileDescriptor)
(JNIEnv* env, jobject thiz, jobject afd, jlong offset, jlong size) {
  const jint fd = libtextclassifier3::GetFdFromAssetFileDescriptor(env, afd);
  const std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd, offset, size));
  return GetNameFromMmap(env, mmap.get());
}
