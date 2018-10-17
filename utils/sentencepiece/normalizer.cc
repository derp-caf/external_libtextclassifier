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

#include "utils/sentencepiece/normalizer.h"

#include "utils/base/logging.h"
#include "utils/strings/utf8.h"

namespace libtextclassifier3 {

std::string Normalizer::Normalize(StringPiece input) const {
  std::string normalized;

  // Ignores heading space.
  if (remove_extra_whitespaces_) {
    while (!input.empty()) {
      const auto suffix_and_length = NormalizePrefix(input);
      if (suffix_and_length.second <= 0) {
        TC3_LOG(ERROR) << "Consumed string is empty.";
        return normalized;
      }
      if (suffix_and_length.first.size() != 1 ||
          suffix_and_length.first[0] != ' ') {
        break;
      }
      input.RemovePrefix(suffix_and_length.second);
    }
  }

  if (input.empty()) {
    return normalized;
  }

  // Reserves the output buffer to avoid re-allocations.
  const int kReservedSize = input.size() * 3;
  normalized.reserve(kReservedSize);

  // Replaces white space with U+2581 (LOWER ONE EIGHT BLOCK)
  // if escape_whitespaces() is set (default = true).
  const StringPiece kSpaceSymbol = "\xe2\x96\x81";

  // Adds a space symbol as a prefix (default is true)
  // With this prefix, "world" and "hello world" are converted into
  // "_world" and "_hello_world", which help the trainer to extract
  // "_world" as one symbol.
  if (add_dummy_prefix_) {
    if (escape_whitespaces_) {
      normalized.append(kSpaceSymbol.data(), kSpaceSymbol.size());
    } else {
      normalized.append(" ");
    }
  }

  bool is_prev_space = remove_extra_whitespaces_;
  while (!input.empty()) {
    auto p = NormalizePrefix(input);
    if (p.second <= 0) {
      TC3_LOG(ERROR) << "Consumed string is empty.";
      return normalized;
    }

    StringPiece sp = p.first;

    // Removes heading spaces in sentence piece,
    // if the previous sentence piece ends with whitespace.
    while (is_prev_space && ConsumePrefix(&sp, " ")) {
    }

    if (!sp.empty()) {
      const char *data = sp.data();
      for (int n = 0; n < sp.size(); ++n) {
        if (escape_whitespaces_ && data[n] == ' ') {
          normalized.append(kSpaceSymbol.data(), kSpaceSymbol.size());
        } else {
          normalized += data[n];
        }
      }
      // Checks whether the last character of sp is whitespace.
      is_prev_space = EndsWith(sp, " ");
    }
    input.RemovePrefix(p.second);
    is_prev_space = is_prev_space && remove_extra_whitespaces_;
  }

  // Ignores tailing space.
  if (remove_extra_whitespaces_) {
    const StringPiece space = escape_whitespaces_ ? kSpaceSymbol : " ";
    while (EndsWith(normalized, space)) {
      const int length = normalized.size() - space.size();
      normalized.resize(length);
    }
  }
  return normalized;
}

std::pair<StringPiece, int> Normalizer::NormalizePrefix(
    StringPiece input) const {
  std::pair<StringPiece, int> result;
  if (input.empty()) return result;
  const TrieMatch match = charsmap_trie_.LongestPrefixMatch(input);
  const bool no_match = match.match_length <= 0;
  if (no_match) {
    const int char_length = ValidUTF8CharLength(input.data(), input.size());
    if (char_length <= 0) {
      // Found a malformed utf8.
      // The rune is set to be 0xFFFD (REPLACEMENT CHARACTER),
      // which is a valid Unicode of three bytes in utf8,
      // but here we only consume one byte.
      static const char kReplacementChar[] = "\xEF\xBF\xBD";
      result.first = StringPiece(kReplacementChar, 3);
      result.second = 1;  // Consumes 1 byte, buts emit 0xFFFD.
    } else {
      result.first = StringPiece(input.data(), char_length);
      result.second = char_length;
    }
  } else {
    TC3_CHECK(match.id >= 0 && match.id < charsmap_normalized_.size());
    result.first = StringPiece(&charsmap_normalized_.data()[match.id]);
    result.second = match.match_length;
  }
  return result;
}

}  // namespace libtextclassifier3
