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

bool DoubleArrayTrie::GatherPrefixMatches(
    StringPiece input, const std::function<void(TrieMatch)>& update_fn) const {
  unsigned int pos = 0;
  if (nodes_length_ == 0) {
    TC3_LOG(WARNING) << "Trie is empty. Skipping.";
    return true;
  }
  pos = offset(0);
  for (int i = 0; i < input.size(); i++) {
    if (input[i] == 0) {
      break;
    }
    pos ^= static_cast<unsigned char>(input[i]);
    // We exhausted the trie, no more matches possible.
    if (pos < 0 || pos >= nodes_length_) {
      break;
    }
    if (label(pos) != input[i]) {
      break;
    }
    const bool node_has_leaf = has_leaf(pos);
    pos ^= offset(pos);
    if (pos < 0 || pos > nodes_length_) {
      TC3_LOG(ERROR) << "Out-of-bounds trie search position.";
      return false;
    }
    if (node_has_leaf) {
      update_fn(TrieMatch(/*id=*/value(pos), /*match_length=*/i + 1));
    }
  }
  return true;
}

bool DoubleArrayTrie::FindAllPrefixMatches(
    StringPiece input, std::vector<TrieMatch>* matches) const {
  return GatherPrefixMatches(
      input, [matches](const TrieMatch match) { matches->push_back(match); });
}

bool DoubleArrayTrie::LongestPrefixMatch(StringPiece input,
                                         TrieMatch* longest_match) const {
  *longest_match = TrieMatch();
  return GatherPrefixMatches(input, [longest_match](const TrieMatch match) {
    *longest_match = match;
  });
}

}  // namespace libtextclassifier3
