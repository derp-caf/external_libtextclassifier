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

#ifndef LIBTEXTCLASSIFIER_UTILS_REGEX_MATCH_H_
#define LIBTEXTCLASSIFIER_UTILS_REGEX_MATCH_H_

#include "utils/flatbuffers.h"
#include "utils/flatbuffers_generated.h"
#include "utils/utf8/unilib.h"

namespace libtextclassifier3 {
// Sets a field in the flatbuffer from a regex match group.
// Returns true if successful, and false if the field couldn't be set.
bool SetFieldFromCapturingGroup(const int group_id,
                                const FlatbufferFieldPath* field_path,
                                UniLib::RegexMatcher* matcher,
                                ReflectiveFlatbuffer* flatbuffer);
}  // namespace libtextclassifier3

#endif  // LIBTEXTCLASSIFIER_UTILS_REGEX_MATCH_H_
