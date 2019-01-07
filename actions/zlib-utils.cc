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

#include "actions/zlib-utils.h"

#include <memory>

#include "utils/base/logging.h"
#include "utils/zlib/zlib.h"

namespace libtextclassifier3 {

// Compress rule fields in the model.
bool CompressActionsModel(ActionsModelT* model) {
  std::unique_ptr<ZlibCompressor> zlib_compressor = ZlibCompressor::Instance();
  if (!zlib_compressor) {
    TC3_LOG(ERROR) << "Cannot compress model.";
    return false;
  }

  // Compress regex rules.
  if (model->rules != nullptr) {
    for (int i = 0; i < model->rules->rule.size(); i++) {
      RulesModel_::RuleT* rule = model->rules->rule[i].get();
      rule->compressed_pattern.reset(new CompressedBufferT);
      zlib_compressor->Compress(rule->pattern, rule->compressed_pattern.get());
      rule->pattern.clear();
    }
  }

  return true;
}

bool DecompressActionsModel(ActionsModelT* model) {
  std::unique_ptr<ZlibDecompressor> zlib_decompressor =
      ZlibDecompressor::Instance();
  if (!zlib_decompressor) {
    TC3_LOG(ERROR) << "Cannot initialize decompressor.";
    return false;
  }

  // Decompress regex rules.
  if (model->rules != nullptr) {
    for (int i = 0; i < model->rules->rule.size(); i++) {
      RulesModel_::RuleT* rule = model->rules->rule[i].get();
      if (!zlib_decompressor->MaybeDecompress(rule->compressed_pattern.get(),
                                              &rule->pattern)) {
        TC3_LOG(ERROR) << "Cannot decompress pattern: " << i;
        return false;
      }
      rule->compressed_pattern.reset(nullptr);
    }
  }

  return true;
}

std::string CompressSerializedActionsModel(const std::string& model) {
  std::unique_ptr<ActionsModelT> unpacked_model =
      UnPackActionsModel(model.c_str());
  TC3_CHECK(unpacked_model != nullptr);
  TC3_CHECK(CompressActionsModel(unpacked_model.get()));
  flatbuffers::FlatBufferBuilder builder;
  FinishActionsModelBuffer(builder,
                           ActionsModel::Pack(builder, unpacked_model.get()));
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()),
                     builder.GetSize());
}

}  // namespace libtextclassifier3
