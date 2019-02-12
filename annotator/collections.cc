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

#include "annotator/collections.h"

namespace libtextclassifier3 {

const std::string& Collections::kOther =
    *[]() { return new std::string("other"); }();
const std::string& Collections::kPhone =
    *[]() { return new std::string("phone"); }();
const std::string& Collections::kAddress =
    *[]() { return new std::string("address"); }();
const std::string& Collections::kDate =
    *[]() { return new std::string("date"); }();
const std::string& Collections::kUrl =
    *[]() { return new std::string("url"); }();
const std::string& Collections::kFlight =
    *[]() { return new std::string("flight"); }();
const std::string& Collections::kEmail =
    *[]() { return new std::string("email"); }();
const std::string& Collections::kIban =
    *[]() { return new std::string("iban"); }();
const std::string& Collections::kPaymentCard =
    *[]() { return new std::string("payment_card"); }();
const std::string& Collections::kIsbn =
    *[]() { return new std::string("isbn"); }();
const std::string& Collections::kTrackingNumber =
    *[]() { return new std::string("tracking_number"); }();
const std::string& Collections::kContact =
    *[]() { return new std::string("contact"); }();

}  // namespace libtextclassifier3
