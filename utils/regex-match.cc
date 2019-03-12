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

#include "utils/regex-match.h"

namespace libtextclassifier3 {

bool SetFieldFromCapturingGroup(const int group_id,
                                const FlatbufferFieldPath* field_path,
                                UniLib::RegexMatcher* matcher,
                                ReflectiveFlatbuffer* flatbuffer) {
  int status = UniLib::RegexMatcher::kNoError;
  std::string group_text = matcher->Group(group_id, &status).ToUTF8String();
  if (status != UniLib::RegexMatcher::kNoError || group_text.empty()) {
    return false;
  }
  return flatbuffer->Set(field_path, group_text);
}

}  // namespace libtextclassifier3
