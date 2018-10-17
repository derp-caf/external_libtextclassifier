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

#include "utils/java/jni-cache.h"

#include "utils/base/logging.h"

namespace libtextclassifier3 {

JniCache::JniCache(JavaVM* jvm)
    : jvm(jvm),
      string_class(nullptr, jvm),
      string_utf8(nullptr, jvm),
      pattern_class(nullptr, jvm),
      matcher_class(nullptr, jvm),
      locale_class(nullptr, jvm),
      locale_us(nullptr, jvm),
      breakiterator_class(nullptr, jvm),
      integer_class(nullptr, jvm),
      calendar_class(nullptr, jvm),
      timezone_class(nullptr, jvm) {}

// The macros below are intended to reduce the boilerplate in Create and avoid
// easily introduced copy/paste errors.
#define TC3_CHECK_JNI_PTR(PTR) \
  TC3_DCHECK(PTR);             \
  if (!(PTR)) return nullptr;

#define TC3_GET_CLASS(FIELD, NAME)                                       \
  result->FIELD##_class = MakeGlobalRef(env->FindClass(NAME), env, jvm); \
  TC3_CHECK_JNI_PTR(result->FIELD##_class)

#define TC3_GET_METHOD(CLASS, FIELD, NAME, SIGNATURE)                 \
  result->CLASS##_##FIELD =                                           \
      env->GetMethodID(result->CLASS##_class.get(), NAME, SIGNATURE); \
  TC3_CHECK_JNI_PTR(result->CLASS##_##FIELD)

#define TC3_GET_OPTIONAL_STATIC_METHOD(CLASS, FIELD, NAME, SIGNATURE)       \
  result->CLASS##_##FIELD =                                                 \
      env->GetStaticMethodID(result->CLASS##_class.get(), NAME, SIGNATURE); \
  env->ExceptionClear();

#define TC3_GET_STATIC_METHOD(CLASS, FIELD, NAME, SIGNATURE)                \
  result->CLASS##_##FIELD =                                                 \
      env->GetStaticMethodID(result->CLASS##_class.get(), NAME, SIGNATURE); \
  TC3_CHECK_JNI_PTR(result->CLASS##_##FIELD)

#define TC3_GET_STATIC_OBJECT_FIELD(CLASS, FIELD, NAME, SIGNATURE)         \
  const jfieldID CLASS##_##FIELD##_field =                                 \
      env->GetStaticFieldID(result->CLASS##_class.get(), NAME, SIGNATURE); \
  TC3_CHECK_JNI_PTR(CLASS##_##FIELD##_field)                               \
  result->CLASS##_##FIELD =                                                \
      MakeGlobalRef(env->GetStaticObjectField(result->CLASS##_class.get(), \
                                              CLASS##_##FIELD##_field),    \
                    env, jvm);                                             \
  TC3_CHECK_JNI_PTR(result->CLASS##_##FIELD)

#define TC3_GET_STATIC_INT_FIELD(CLASS, FIELD, NAME)                 \
  const jfieldID CLASS##_##FIELD##_field =                           \
      env->GetStaticFieldID(result->CLASS##_class.get(), NAME, "I"); \
  TC3_CHECK_JNI_PTR(CLASS##_##FIELD##_field)                         \
  result->CLASS##_##FIELD = env->GetStaticIntField(                  \
      result->CLASS##_class.get(), CLASS##_##FIELD##_field);         \
  TC3_CHECK_JNI_PTR(result->CLASS##_##FIELD)

std::unique_ptr<JniCache> JniCache::Create(JNIEnv* env) {
  if (env == nullptr) {
    return nullptr;
  }
  JavaVM* jvm = nullptr;
  if (JNI_OK != env->GetJavaVM(&jvm) || jvm == nullptr) {
    return nullptr;
  }
  std::unique_ptr<JniCache> result(new JniCache(jvm));

  // String
  TC3_GET_CLASS(string, "java/lang/String");
  TC3_GET_METHOD(string, init_bytes_charset, "<init>",
                 "([BLjava/lang/String;)V");
  TC3_GET_METHOD(string, code_point_count, "codePointCount", "(II)I");
  TC3_GET_METHOD(string, length, "length", "()I");
  result->string_utf8 = MakeGlobalRef(env->NewStringUTF("UTF-8"), env, jvm);
  TC3_CHECK_JNI_PTR(result->string_utf8)

  // Pattern
  TC3_GET_CLASS(pattern, "java/util/regex/Pattern");
  TC3_GET_STATIC_METHOD(pattern, compile, "compile",
                        "(Ljava/lang/String;)Ljava/util/regex/Pattern;");
  TC3_GET_METHOD(pattern, matcher, "matcher",
                 "(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;");

  // Matcher
  TC3_GET_CLASS(matcher, "java/util/regex/Matcher");
  TC3_GET_METHOD(matcher, matches, "matches", "()Z");
  TC3_GET_METHOD(matcher, find, "find", "()Z");
  TC3_GET_METHOD(matcher, reset, "reset", "()Ljava/util/regex/Matcher;");
  TC3_GET_METHOD(matcher, start_idx, "start", "(I)I");
  TC3_GET_METHOD(matcher, end_idx, "end", "(I)I");
  TC3_GET_METHOD(matcher, group, "group", "()Ljava/lang/String;");
  TC3_GET_METHOD(matcher, group_idx, "group", "(I)Ljava/lang/String;");

  // Locale
  TC3_GET_CLASS(locale, "java/util/Locale");
  TC3_GET_STATIC_OBJECT_FIELD(locale, us, "US", "Ljava/util/Locale;");
  TC3_GET_METHOD(locale, init_string, "<init>", "(Ljava/lang/String;)V");
  TC3_GET_OPTIONAL_STATIC_METHOD(locale, for_language_tag, "forLanguageTag",
                                 "(Ljava/lang/String;)Ljava/util/Locale;");

  // BreakIterator
  TC3_GET_CLASS(breakiterator, "java/text/BreakIterator");
  TC3_GET_STATIC_METHOD(breakiterator, getwordinstance, "getWordInstance",
                        "(Ljava/util/Locale;)Ljava/text/BreakIterator;");
  TC3_GET_METHOD(breakiterator, settext, "setText", "(Ljava/lang/String;)V");
  TC3_GET_METHOD(breakiterator, next, "next", "()I");

  // Integer
  TC3_GET_CLASS(integer, "java/lang/Integer");
  TC3_GET_STATIC_METHOD(integer, parse_int, "parseInt",
                        "(Ljava/lang/String;)I");

  // Calendar.
  TC3_GET_CLASS(calendar, "java/util/Calendar");
  TC3_GET_STATIC_METHOD(
      calendar, get_instance, "getInstance",
      "(Ljava/util/TimeZone;Ljava/util/Locale;)Ljava/util/Calendar;");
  TC3_GET_METHOD(calendar, get_first_day_of_week, "getFirstDayOfWeek", "()I");
  TC3_GET_METHOD(calendar, get_time_in_millis, "getTimeInMillis", "()J");
  TC3_GET_METHOD(calendar, set_time_in_millis, "setTimeInMillis", "(J)V");
  TC3_GET_METHOD(calendar, add, "add", "(II)V");
  TC3_GET_METHOD(calendar, get, "get", "(I)I");
  TC3_GET_METHOD(calendar, set, "set", "(II)V");
  TC3_GET_STATIC_INT_FIELD(calendar, zone_offset, "ZONE_OFFSET");
  TC3_GET_STATIC_INT_FIELD(calendar, dst_offset, "DST_OFFSET");
  TC3_GET_STATIC_INT_FIELD(calendar, year, "YEAR");
  TC3_GET_STATIC_INT_FIELD(calendar, month, "MONTH");
  TC3_GET_STATIC_INT_FIELD(calendar, day_of_year, "DAY_OF_YEAR");
  TC3_GET_STATIC_INT_FIELD(calendar, day_of_month, "DAY_OF_MONTH");
  TC3_GET_STATIC_INT_FIELD(calendar, day_of_week, "DAY_OF_WEEK");
  TC3_GET_STATIC_INT_FIELD(calendar, hour_of_day, "HOUR_OF_DAY");
  TC3_GET_STATIC_INT_FIELD(calendar, minute, "MINUTE");
  TC3_GET_STATIC_INT_FIELD(calendar, second, "SECOND");
  TC3_GET_STATIC_INT_FIELD(calendar, millisecond, "MILLISECOND");
  TC3_GET_STATIC_INT_FIELD(calendar, sunday, "SUNDAY");
  TC3_GET_STATIC_INT_FIELD(calendar, monday, "MONDAY");
  TC3_GET_STATIC_INT_FIELD(calendar, tuesday, "TUESDAY");
  TC3_GET_STATIC_INT_FIELD(calendar, wednesday, "WEDNESDAY");
  TC3_GET_STATIC_INT_FIELD(calendar, thursday, "THURSDAY");
  TC3_GET_STATIC_INT_FIELD(calendar, friday, "FRIDAY");
  TC3_GET_STATIC_INT_FIELD(calendar, saturday, "SATURDAY");

  // TimeZone.
  TC3_GET_CLASS(timezone, "java/util/TimeZone");
  TC3_GET_STATIC_METHOD(timezone, get_timezone, "getTimeZone",
                        "(Ljava/lang/String;)Ljava/util/TimeZone;");

  return result;
}

#undef TC3_GET_STATIC_INT_FIELD
#undef TC3_GET_STATIC_OBJECT_FIELD
#undef TC3_GET_STATIC_METHOD
#undef TC3_GET_METHOD
#undef TC3_GET_CLASS
#undef TC3_CHECK_JNI_PTR

JNIEnv* JniCache::GetEnv() const {
  void* env;
  if (JNI_OK == jvm->GetEnv(&env, JNI_VERSION_1_4)) {
    return reinterpret_cast<JNIEnv*>(env);
  } else {
    TC3_LOG(ERROR) << "JavaICU UniLib used on unattached thread";
    return nullptr;
  }
}

bool JniCache::ExceptionCheckAndClear() const {
  JNIEnv* env = GetEnv();
  TC3_CHECK(env != nullptr);
  const bool result = env->ExceptionCheck();
  if (result) {
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
  return result;
}

ScopedLocalRef<jstring> JniCache::ConvertToJavaString(
    const UnicodeText& text) const {
  // Create java byte array.
  JNIEnv* jenv = GetEnv();
  const ScopedLocalRef<jbyteArray> text_java_utf8(
      jenv->NewByteArray(text.size_bytes()), jenv);
  if (!text_java_utf8) {
    return nullptr;
  }

  jenv->SetByteArrayRegion(text_java_utf8.get(), 0, text.size_bytes(),
                           reinterpret_cast<const jbyte*>(text.data()));

  // Create the string with a UTF-8 charset.
  return ScopedLocalRef<jstring>(
      reinterpret_cast<jstring>(
          jenv->NewObject(string_class.get(), string_init_bytes_charset,
                          text_java_utf8.get(), string_utf8.get())),
      jenv);
}

}  // namespace libtextclassifier3
