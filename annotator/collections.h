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

#ifndef LIBTEXTCLASSIFIER_ANNOTATOR_COLLECTIONS_H_
#define LIBTEXTCLASSIFIER_ANNOTATOR_COLLECTIONS_H_

#include <string>

namespace libtextclassifier3 {

// String collection names for various classes.
class Collections {
 public:
  static const std::string& kOther;
  static const std::string& kPhone;
  static const std::string& kAddress;
  static const std::string& kDate;
  static const std::string& kUrl;
  static const std::string& kFlight;
  static const std::string& kEmail;
  static const std::string& kIban;
  static const std::string& kPaymentCard;
  static const std::string& kIsbn;
  static const std::string& kTrackingNumber;
  static const std::string& kContact;
};

}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_ANNOTATOR_COLLECTIONS_H_
