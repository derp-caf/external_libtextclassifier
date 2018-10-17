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

#ifndef NLP_SAFT_COMPONENTS_LANG_ID_MOBILE_LANG_ID_BRAIN_INTERFACE_H_
#define NLP_SAFT_COMPONENTS_LANG_ID_MOBILE_LANG_ID_BRAIN_INTERFACE_H_

#include <string>
#include <vector>

#include "lang_id/common/embedding-feature-extractor.h"
#include "lang_id/common/fel/feature-extractor.h"
#include "lang_id/common/fel/task-context.h"
#include "lang_id/common/fel/workspace.h"
#include "lang_id/common/lite_base/attributes.h"
#include "lang_id/features/light-sentence-features.h"
#include "lang_id/light-sentence.h"

// TODO(abakalov): Add a test.
namespace libtextclassifier3 {
namespace mobile {
namespace lang_id {

// Specialization of EmbeddingFeatureExtractor that extracts from LightSentence.
class LangIdEmbeddingFeatureExtractor
    : public EmbeddingFeatureExtractor<LightSentenceExtractor, LightSentence> {
 public:
  const string ArgPrefix() const override { return "language_identifier"; }
};

// Similar to the inference (processing) part of SaftBrainInterface from
// nlp/saft/components/common/brain/saft-brain-interface.h
//
// Handles sentence -> numeric_features and numeric_prediction -> language
// conversions.
class LangIdBrainInterface {
 public:
  // Requests/initializes resources and parameters.
  SAFTM_MUST_USE_RESULT bool SetupForProcessing(TaskContext *context) {
    return feature_extractor_.Setup(context);
  }

  SAFTM_MUST_USE_RESULT bool InitForProcessing(TaskContext *context) {
    if (!feature_extractor_.Init(context)) return false;
    feature_extractor_.RequestWorkspaces(&workspace_registry_);
    return true;
  }

  // Extract features from sentence.  On return, FeatureVector features[i]
  // contains the features for the embedding space #i.
  void GetFeatures(LightSentence *sentence,
                   std::vector<FeatureVector> *features) const {
    WorkspaceSet workspace;
    workspace.Reset(workspace_registry_);
    feature_extractor_.Preprocess(&workspace, sentence);
    return feature_extractor_.ExtractFeatures(workspace, *sentence, features);
  }

  int NumEmbeddings() const {
    return feature_extractor_.NumEmbeddings();
  }

 private:
  // Typed feature extractor for embeddings.
  LangIdEmbeddingFeatureExtractor feature_extractor_;

  // The registry of shared workspaces in the feature extractor.
  WorkspaceRegistry workspace_registry_;
};

}  // namespace lang_id
}  // namespace mobile
}  // namespace nlp_saft

#endif  // NLP_SAFT_COMPONENTS_LANG_ID_MOBILE_LANG_ID_BRAIN_INTERFACE_H_
