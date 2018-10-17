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

#include "utils/sentencepiece/double_array_trie.h"
#include "utils/base/logging.h"

namespace libtextclassifier3 {

void DoubleArrayTrie::GatherPrefixMatches(
    StringPiece input, const std::function<void(TrieMatch)>& update_fn) const {
  int pos = 0;
  TC3_CHECK(pos >= 0 && pos < nodes_length_);
  pos = offset(0);
  for (int i = 0; i < input.size(); i++) {
    pos ^= input[i];
    TC3_CHECK(pos >= 0 && pos < nodes_length_);
    if (label(pos) != input[i]) {
      break;
    }
    const bool node_has_leaf = has_leaf(pos);
    pos ^= offset(pos);
    TC3_CHECK(pos >= 0 && pos < nodes_length_);
    if (node_has_leaf) {
      update_fn(TrieMatch(/*id=*/value(pos), /*match_length=*/i + 1));
    }
  }
}

std::vector<TrieMatch> DoubleArrayTrie::FindAllPrefixMatches(
    StringPiece input) const {
  std::vector<TrieMatch> result;
  GatherPrefixMatches(
      input, [&result](const TrieMatch match) { result.push_back(match); });
  return result;
}

TrieMatch DoubleArrayTrie::LongestPrefixMatch(StringPiece input) const {
  TrieMatch longest_match;
  GatherPrefixMatches(input, [&longest_match](const TrieMatch match) {
    longest_match = match;
  });
  return longest_match;
}

}  // namespace libtextclassifier3
