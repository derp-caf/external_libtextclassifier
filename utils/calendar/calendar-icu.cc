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

#include "utils/calendar/calendar-icu.h"

#include <memory>

#include "utils/base/macros.h"
#include "utils/calendar/calendar-common.h"

namespace libtextclassifier3 {
namespace {

// Make sure the constants are compatible with ICU.
#define TC3_CHECK_DAY_CONSTANT(NAME)                                  \
  static_assert(static_cast<int>(UCalendarDaysOfWeek::UCAL_##NAME) == \
                    static_cast<int>(DateParseData::NAME),            \
                "Mismatching constant value for " #NAME);
TC3_CHECK_DAY_CONSTANT(SUNDAY)
TC3_CHECK_DAY_CONSTANT(MONDAY)
TC3_CHECK_DAY_CONSTANT(TUESDAY)
TC3_CHECK_DAY_CONSTANT(WEDNESDAY)
TC3_CHECK_DAY_CONSTANT(THURSDAY)
TC3_CHECK_DAY_CONSTANT(FRIDAY)
TC3_CHECK_DAY_CONSTANT(SATURDAY)
#undef TC3_CHECK_DAY_CONSTANT

// Generic version of icu::Calendar::add with error checking.
bool CalendarAdd(UCalendarDateFields field, int value,
                 icu::Calendar* calendar) {
  UErrorCode status = U_ZERO_ERROR;
  calendar->add(field, value, status);
  if (U_FAILURE(status)) {
    TC3_LOG(ERROR) << "failed to add " << field;
    return false;
  }
  return true;
}

// Generic version of icu::Calendar::get with error checking.
bool CalendarGet(UCalendarDateFields field, int* value,
                 icu::Calendar* calendar) {
  UErrorCode status = U_ZERO_ERROR;
  *value = calendar->get(field, status);
  if (U_FAILURE(status)) {
    TC3_LOG(ERROR) << "failed to get " << field;
    return false;
  }
  return true;
}

// Generic version of icu::Calendar::set with error checking.
bool CalendarSet(UCalendarDateFields field, int value,
                 icu::Calendar* calendar) {
  calendar->set(field, value);
  return true;
}

}  // namespace

bool Calendar::Initialize(const std::string& time_zone,
                          const std::string& locale, int64 time_ms_utc) {
  UErrorCode status = U_ZERO_ERROR;
  calendar_.reset(icu::Calendar::createInstance(
      icu::Locale::createFromName(locale.c_str()), status));
  if (U_FAILURE(status)) {
    TC3_LOG(ERROR) << "error getting calendar instance";
    return false;
  }
  calendar_->adoptTimeZone(
      icu::TimeZone::createTimeZone(icu::UnicodeString::fromUTF8(time_zone)));
  calendar_->setTime(time_ms_utc, status);
  if (U_FAILURE(status)) {
    TC3_LOG(ERROR) << "failed to set time";
    return false;
  }
  return true;
}

bool Calendar::GetFirstDayOfWeek(int* value) const {
  *value = calendar_->getFirstDayOfWeek();
  return true;
}

bool Calendar::GetTimeInMillis(int64* value) const {
  UErrorCode status = U_ZERO_ERROR;
  *value = calendar_->getTime(status);
  if (U_FAILURE(status)) {
    TC3_LOG(ERROR) << "error getting time from instance";
    return false;
  }
  return true;
}

// Below is the boilerplate code for implementing the specialisations of
// get/set/add for the various field types.
#define TC3_DEFINE_FIELD_ACCESSOR(NAME, CONST, KIND, TYPE)          \
  bool Calendar::KIND##NAME(TYPE value) const {                     \
    return Calendar##KIND(UCalendarDateFields::UCAL_##CONST, value, \
                          calendar_.get());                         \
  }
#define TC3_DEFINE_ADD(NAME, CONST) \
  TC3_DEFINE_FIELD_ACCESSOR(NAME, CONST, Add, int)
#define TC3_DEFINE_GET(NAME, CONST) \
  TC3_DEFINE_FIELD_ACCESSOR(NAME, CONST, Get, int*)
#define TC3_DEFINE_SET(NAME, CONST) \
  TC3_DEFINE_FIELD_ACCESSOR(NAME, CONST, Set, int)

TC3_DEFINE_ADD(DayOfMonth, DAY_OF_MONTH)
TC3_DEFINE_ADD(Year, YEAR)
TC3_DEFINE_ADD(Month, MONTH)
TC3_DEFINE_GET(DayOfWeek, DAY_OF_WEEK)
TC3_DEFINE_SET(ZoneOffset, ZONE_OFFSET)
TC3_DEFINE_SET(DstOffset, DST_OFFSET)
TC3_DEFINE_SET(Year, YEAR)
TC3_DEFINE_SET(Month, MONTH)
TC3_DEFINE_SET(DayOfYear, DAY_OF_YEAR)
TC3_DEFINE_SET(DayOfMonth, DAY_OF_MONTH)
TC3_DEFINE_SET(DayOfWeek, DAY_OF_WEEK)
TC3_DEFINE_SET(HourOfDay, HOUR_OF_DAY)
TC3_DEFINE_SET(Minute, MINUTE)
TC3_DEFINE_SET(Second, SECOND)
TC3_DEFINE_SET(Millisecond, MILLISECOND)

#undef TC3_DEFINE_FIELD_ACCESSOR
#undef TC3_DEFINE_ADD
#undef TC3_DEFINE_GET
#undef TC3_DEFINE_SET

}  // namespace libtextclassifier3
