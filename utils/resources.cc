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

#include "utils/resources.h"
#include "utils/base/logging.h"

namespace libtextclassifier3 {
namespace {
bool isWildcardMatch(const flatbuffers::String* left,
                     const std::string& right) {
  return (left == nullptr || right.empty());
}

bool isExactMatch(const flatbuffers::String* left, const std::string& right) {
  if (left == nullptr) {
    return right.empty();
  }
  return left->str() == right;
}
}  // namespace

int Resources::LocaleMatch(const Locale& locale,
                           const LanguageTag* entry_locale) const {
  int match = LOCALE_NO_MATCH;
  if (isExactMatch(entry_locale->language(), locale.Language())) {
    match |= LOCALE_LANGUAGE_MATCH;
  } else if (isWildcardMatch(entry_locale->language(), locale.Language())) {
    match |= LOCALE_LANGUAGE_WILDCARD_MATCH;
  }

  if (isExactMatch(entry_locale->script(), locale.Script())) {
    match |= LOCALE_SCRIPT_MATCH;
  } else if (isWildcardMatch(entry_locale->script(), locale.Script())) {
    match |= LOCALE_SCRIPT_WILDCARD_MATCH;
  }

  if (isExactMatch(entry_locale->region(), locale.Region())) {
    match |= LOCALE_REGION_MATCH;
  } else if (isWildcardMatch(entry_locale->region(), locale.Region())) {
    match |= LOCALE_REGION_WILDCARD_MATCH;
  }

  return match;
}

const ResourceEntry* Resources::FindResource(
    const StringPiece resource_name) const {
  if (resources_ == nullptr || resources_->resource_entry() == nullptr) {
    TC3_LOG(ERROR) << "No resources defined.";
    return nullptr;
  }
  const ResourceEntry* entry =
      resources_->resource_entry()->LookupByKey(resource_name.data());
  if (entry == nullptr) {
    TC3_LOG(ERROR) << "Resource " << resource_name.ToString() << " not found";
    return nullptr;
  }
  return entry;
}

bool Resources::GetResourceContent(const Locale& locale,
                                   const StringPiece resource_name,
                                   StringPiece* result) const {
  const ResourceEntry* entry = FindResource(resource_name);
  if (entry == nullptr || entry->resource() == nullptr) {
    return false;
  }

  // Find best match based on locale.
  int best_matching_resource = -1;
  int best_match = LOCALE_NO_MATCH;
  const auto* entry_resources = entry->resource();
  for (int i = 0; i < entry_resources->size(); i++) {
    int candidate_match = LocaleMatch(
        locale, resources_->locale()->Get(entry_resources->Get(i)->locale()));

    // Only consider if at least the language matches.
    if ((candidate_match & LOCALE_LANGUAGE_MATCH) == 0 &&
        (candidate_match & LOCALE_LANGUAGE_WILDCARD_MATCH) == 0) {
      continue;
    }

    if (candidate_match > best_match) {
      best_match = candidate_match;
      best_matching_resource = i;
    }
  }

  if (best_matching_resource < 0) {
    TC3_LOG(ERROR) << "Couldn't find locale matching resource.";
    return false;
  }

  const auto* content = entry_resources->Get(best_matching_resource)->content();
  *result = StringPiece(content->c_str(), content->Length());
  return true;
}

}  // namespace libtextclassifier3
