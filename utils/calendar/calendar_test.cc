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

// This test serves the purpose of making sure all the different implementations
// of the unspoken CalendarLib interface support the same methods.

#include "utils/calendar/calendar.h"
#include "utils/base/logging.h"

#include "gtest/gtest.h"

namespace libtextclassifier3 {
namespace {

class CalendarTest : public ::testing::Test {
 protected:
  CalendarTest() : INIT_CALENDARLIB_FOR_TESTING(calendarlib_) {}
  CalendarLib calendarlib_;
};

TEST_F(CalendarTest, Interface) {
  int64 time;
  std::string timezone;
  bool result = calendarlib_.InterpretParseData(
      DateParseData{/*field_set_mask=*/0, /*year=*/0, /*month=*/0,
                    /*day_of_month=*/0, /*hour=*/0, /*minute=*/0, /*second=*/0,
                    /*ampm=*/0, /*zone_offset=*/0, /*dst_offset=*/0,
                    static_cast<DateParseData::Relation>(0),
                    static_cast<DateParseData::RelationType>(0),
                    /*relation_distance=*/0},
      0L, "Zurich", "en-CH", GRANULARITY_UNKNOWN, &time);
  TC3_LOG(INFO) << result;
}

#ifdef TC3_CALENDAR_ICU
TEST_F(CalendarTest, RoundingToGranularity) {
  int64 time;
  DateParseData data;
  data.year = 2018;
  data.month = 4;
  data.day_of_month = 25;
  data.hour = 9;
  data.minute = 33;
  data.second = 59;
  data.field_set_mask = DateParseData::YEAR_FIELD | DateParseData::MONTH_FIELD |
                        DateParseData::DAY_FIELD | DateParseData::HOUR_FIELD |
                        DateParseData::MINUTE_FIELD |
                        DateParseData::SECOND_FIELD;
  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_YEAR, &time));
  EXPECT_EQ(time, 1514761200000L /* Jan 01 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_MONTH, &time));
  EXPECT_EQ(time, 1522533600000L /* Apr 01 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_WEEK, &time));
  EXPECT_EQ(time, 1524434400000L /* Mon Apr 23 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"*-CH",
      /*granularity=*/GRANULARITY_WEEK, &time));
  EXPECT_EQ(time, 1524434400000L /* Mon Apr 23 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-US",
      /*granularity=*/GRANULARITY_WEEK, &time));
  EXPECT_EQ(time, 1524348000000L /* Sun Apr 22 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"*-US",
      /*granularity=*/GRANULARITY_WEEK, &time));
  EXPECT_EQ(time, 1524348000000L /* Sun Apr 22 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_DAY, &time));
  EXPECT_EQ(time, 1524607200000L /* Apr 25 2018 00:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_HOUR, &time));
  EXPECT_EQ(time, 1524639600000L /* Apr 25 2018 09:00:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_MINUTE, &time));
  EXPECT_EQ(time, 1524641580000 /* Apr 25 2018 09:33:00 */);

  ASSERT_TRUE(calendarlib_.InterpretParseData(
      data,
      /*reference_time_ms_utc=*/0L, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-CH",
      /*granularity=*/GRANULARITY_SECOND, &time));
  EXPECT_EQ(time, 1524641639000 /* Apr 25 2018 09:33:59 */);
}

TEST_F(CalendarTest, RelativeTimeWeekday) {
  const int field_mask = DateParseData::RELATION_FIELD |
                         DateParseData::RELATION_TYPE_FIELD |
                         DateParseData::RELATION_DISTANCE_FIELD;
  const int64 ref_time = 1524648839000L; /* 25 April 2018 09:33:59 */
  int64 time;

  // Two Weds from now.
  const DateParseData future_wed_parse = {
      field_mask,
      /*year=*/0,
      /*month=*/0,
      /*day_of_month=*/0,
      /*hour=*/0,
      /*minute=*/0,
      /*second=*/0,
      /*ampm=*/0,
      /*zone_offset=*/0,
      /*dst_offset=*/0,
      DateParseData::Relation::FUTURE,
      DateParseData::RelationType::WEDNESDAY,
      /*relation_distance=*/2};
  ASSERT_TRUE(calendarlib_.InterpretParseData(
      future_wed_parse, ref_time, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-US",
      /*granularity=*/GRANULARITY_DAY, &time));
  EXPECT_EQ(time, 1525816800000L /* 9 May 2018 00:00:00 */);

  // Next Wed.
  const DateParseData next_wed_parse = {field_mask,
                                        /*year=*/0,
                                        /*month=*/0,
                                        /*day_of_month=*/0,
                                        /*hour=*/0,
                                        /*minute=*/0,
                                        /*second=*/0,
                                        /*ampm=*/0,
                                        /*zone_offset=*/0,
                                        /*dst_offset=*/0,
                                        DateParseData::Relation::NEXT,
                                        DateParseData::RelationType::WEDNESDAY,
                                        /*relation_distance=*/0};
  ASSERT_TRUE(calendarlib_.InterpretParseData(
      next_wed_parse, ref_time, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-US",
      /*granularity=*/GRANULARITY_DAY, &time));
  EXPECT_EQ(time, 1525212000000L /* 1 May 2018 00:00:00 */);

  // Same Wed.
  const DateParseData same_wed_parse = {field_mask,
                                        /*year=*/0,
                                        /*month=*/0,
                                        /*day_of_month=*/0,
                                        /*hour=*/0,
                                        /*minute=*/0,
                                        /*second=*/0,
                                        /*ampm=*/0,
                                        /*zone_offset=*/0,
                                        /*dst_offset=*/0,
                                        DateParseData::Relation::NEXT_OR_SAME,
                                        DateParseData::RelationType::WEDNESDAY,
                                        /*relation_distance=*/0};
  ASSERT_TRUE(calendarlib_.InterpretParseData(
      same_wed_parse, ref_time, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-US",
      /*granularity=*/GRANULARITY_DAY, &time));
  EXPECT_EQ(time, 1524607200000L /* 25 April 2018 00:00:00 */);

  // Previous Wed.
  const DateParseData last_wed_parse = {field_mask,
                                        /*year=*/0,
                                        /*month=*/0,
                                        /*day_of_month=*/0,
                                        /*hour=*/0,
                                        /*minute=*/0,
                                        /*second=*/0,
                                        /*ampm=*/0,
                                        /*zone_offset=*/0,
                                        /*dst_offset=*/0,
                                        DateParseData::Relation::LAST,
                                        DateParseData::RelationType::WEDNESDAY,
                                        /*relation_distance=*/0};
  ASSERT_TRUE(calendarlib_.InterpretParseData(
      last_wed_parse, ref_time, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-US",
      /*granularity=*/GRANULARITY_DAY, &time));
  EXPECT_EQ(time, 1524002400000L /* 18 April 2018 00:00:00 */);

  // Two Weds ago.
  const DateParseData past_wed_parse = {field_mask,
                                        /*year=*/0,
                                        /*month=*/0,
                                        /*day_of_month=*/0,
                                        /*hour=*/0,
                                        /*minute=*/0,
                                        /*second=*/0,
                                        /*ampm=*/0,
                                        /*zone_offset=*/0,
                                        /*dst_offset=*/0,
                                        DateParseData::Relation::PAST,
                                        DateParseData::RelationType::WEDNESDAY,
                                        /*relation_distance=*/2};
  ASSERT_TRUE(calendarlib_.InterpretParseData(
      past_wed_parse, ref_time, /*reference_timezone=*/"Europe/Zurich",
      /*reference_locale=*/"en-US",
      /*granularity=*/GRANULARITY_DAY, &time));
  EXPECT_EQ(time, 1523397600000L /* 11 April 2018 00:00:00 */);
}
#endif  // TC3_UNILIB_DUMMY

}  // namespace
}  // namespace libtextclassifier3
