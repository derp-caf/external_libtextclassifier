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

#include "actions/actions-suggestions.h"

#include <memory>

#include "actions/lua-actions.h"
#include "actions/types.h"
#include "actions/zlib-utils.h"
#include "utils/base/logging.h"
#include "utils/flatbuffers.h"
#include "utils/lua-utils.h"
#include "utils/regex-match.h"
#include "utils/strings/split.h"
#include "utils/strings/stringpiece.h"
#include "utils/utf8/unicodetext.h"
#include "utils/zlib/zlib_regex.h"
#include "tensorflow/lite/string_util.h"

namespace libtextclassifier3 {

const std::string& ActionsSuggestions::kViewCalendarType =
    *[]() { return new std::string("view_calendar"); }();
const std::string& ActionsSuggestions::kViewMapType =
    *[]() { return new std::string("view_map"); }();
const std::string& ActionsSuggestions::kTrackFlightType =
    *[]() { return new std::string("track_flight"); }();
const std::string& ActionsSuggestions::kOpenUrlType =
    *[]() { return new std::string("open_url"); }();
const std::string& ActionsSuggestions::kSendSmsType =
    *[]() { return new std::string("send_sms"); }();
const std::string& ActionsSuggestions::kCallPhoneType =
    *[]() { return new std::string("call_phone"); }();
const std::string& ActionsSuggestions::kSendEmailType =
    *[]() { return new std::string("send_email"); }();
const std::string& ActionsSuggestions::kShareLocation =
    *[]() { return new std::string("share_location"); }();

namespace {

const ActionsModel* LoadAndVerifyModel(const uint8_t* addr, int size) {
  flatbuffers::Verifier verifier(addr, size);
  if (VerifyActionsModelBuffer(verifier)) {
    return GetActionsModel(addr);
  } else {
    return nullptr;
  }
}

template <typename T>
T ValueOrDefault(const flatbuffers::Table* values, const int32 field_offset,
                 const T default_value) {
  if (values == nullptr) {
    return default_value;
  }
  return values->GetField<T>(field_offset, default_value);
}

}  // namespace

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromUnownedBuffer(
    const uint8_t* buffer, const int size, const UniLib* unilib,
    const std::string& triggering_preconditions_overlay) {
  auto actions = std::unique_ptr<ActionsSuggestions>(new ActionsSuggestions());
  const ActionsModel* model = LoadAndVerifyModel(buffer, size);
  if (model == nullptr) {
    return nullptr;
  }
  actions->model_ = model;
  actions->SetOrCreateUnilib(unilib);
  actions->triggering_preconditions_overlay_buffer_ =
      triggering_preconditions_overlay;
  if (!actions->ValidateAndInitialize()) {
    return nullptr;
  }
  return actions;
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromScopedMmap(
    std::unique_ptr<libtextclassifier3::ScopedMmap> mmap, const UniLib* unilib,
    const std::string& triggering_preconditions_overlay) {
  if (!mmap->handle().ok()) {
    TC3_VLOG(1) << "Mmap failed.";
    return nullptr;
  }
  const ActionsModel* model = LoadAndVerifyModel(
      reinterpret_cast<const uint8_t*>(mmap->handle().start()),
      mmap->handle().num_bytes());
  if (!model) {
    TC3_LOG(ERROR) << "Model verification failed.";
    return nullptr;
  }
  auto actions = std::unique_ptr<ActionsSuggestions>(new ActionsSuggestions());
  actions->model_ = model;
  actions->mmap_ = std::move(mmap);
  actions->SetOrCreateUnilib(unilib);
  actions->triggering_preconditions_overlay_buffer_ =
      triggering_preconditions_overlay;
  if (!actions->ValidateAndInitialize()) {
    return nullptr;
  }
  return actions;
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromFileDescriptor(
    const int fd, const int offset, const int size, const UniLib* unilib,
    const std::string& preconditions_overwrite) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd, offset, size));
  return FromScopedMmap(std::move(mmap), unilib, preconditions_overwrite);
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromFileDescriptor(
    const int fd, const UniLib* unilib,
    const std::string& preconditions_overwrite) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(fd));
  return FromScopedMmap(std::move(mmap), unilib, preconditions_overwrite);
}

std::unique_ptr<ActionsSuggestions> ActionsSuggestions::FromPath(
    const std::string& path, const UniLib* unilib,
    const std::string& preconditions_overwrite) {
  std::unique_ptr<libtextclassifier3::ScopedMmap> mmap(
      new libtextclassifier3::ScopedMmap(path));
  return FromScopedMmap(std::move(mmap), unilib, preconditions_overwrite);
}

void ActionsSuggestions::SetOrCreateUnilib(const UniLib* unilib) {
  if (unilib != nullptr) {
    unilib_ = unilib;
  } else {
    owned_unilib_.reset(new UniLib);
    unilib_ = owned_unilib_.get();
  }
}

bool ActionsSuggestions::ValidateAndInitialize() {
  if (model_ == nullptr) {
    TC3_LOG(ERROR) << "No model specified.";
    return false;
  }

  if (model_->smart_reply_action_type() == nullptr) {
    TC3_LOG(ERROR) << "No smart reply action type specified.";
    return false;
  }

  if (!InitializeTriggeringPreconditions()) {
    TC3_LOG(ERROR) << "Could not initialize preconditions.";
    return false;
  }

  if (model_->locales() &&
      !ParseLocales(model_->locales()->c_str(), &locales_)) {
    TC3_LOG(ERROR) << "Could not parse model supported locales.";
    return false;
  }

  if (model_->tflite_model_spec()) {
    model_executor_ = TfLiteModelExecutor::FromBuffer(
        model_->tflite_model_spec()->tflite_model());
    if (!model_executor_) {
      TC3_LOG(ERROR) << "Could not initialize model executor.";
      return false;
    }
  }

  std::unique_ptr<ZlibDecompressor> decompressor = ZlibDecompressor::Instance();
  if (!InitializeRules(decompressor.get())) {
    TC3_LOG(ERROR) << "Could not initialize rules.";
    return false;
  }

  if (model_->actions_entity_data_schema()) {
    entity_data_schema_ = LoadAndVerifyFlatbuffer<reflection::Schema>(
        model_->actions_entity_data_schema()->Data(),
        model_->actions_entity_data_schema()->size());
    if (entity_data_schema_ == nullptr) {
      TC3_LOG(ERROR) << "Could not load entity data schema data.";
      return false;
    }

    entity_data_builder_.reset(
        new ReflectiveFlatbufferBuilder(entity_data_schema_));
  } else {
    entity_data_schema_ = nullptr;
  }

  std::string actions_script;
  if (GetUncompressedString(model_->lua_actions_script(),
                            model_->compressed_lua_actions_script(),
                            decompressor.get(), &actions_script) &&
      !actions_script.empty()) {
    if (!Compile(actions_script, &lua_bytecode_)) {
      TC3_LOG(ERROR) << "Could not precompile lua actions snippet.";
      return false;
    }
  }

  if (!(ranker_ = ActionsSuggestionsRanker::CreateActionsSuggestionsRanker(
            model_->ranking_options(), decompressor.get(),
            model_->smart_reply_action_type()->str()))) {
    TC3_LOG(ERROR) << "Could not create an action suggestions ranker.";
    return false;
  }

  // Create feature processor if specified.
  const ActionsTokenFeatureProcessorOptions* options =
      model_->feature_processor_options();
  if (options != nullptr) {
    if (options->tokenizer_options() == nullptr) {
      TC3_LOG(ERROR) << "No tokenizer options specified.";
      return false;
    }

    feature_processor_.reset(new ActionsFeatureProcessor(options, unilib_));
    embedding_executor_ = TFLiteEmbeddingExecutor::FromBuffer(
        options->embedding_model(), options->embedding_size(),
        options->embedding_quantization_bits());

    if (embedding_executor_ == nullptr) {
      TC3_LOG(ERROR) << "Could not initialize embedding executor.";
      return false;
    }

    // Cache embedding of padding token.
    if (!feature_processor_->AppendTokenFeatures(
            Token(), embedding_executor_.get(), &embedded_padding_token_)) {
      TC3_LOG(ERROR) << "Could not run token feature extractor.";
      return false;
    }
    token_embedding_size_ = feature_processor_->GetTokenEmbeddingSize();
  }

  // Create low confidence model if specified.
  if (model_->low_confidence_ngram_model() != nullptr) {
    ngram_model_ = NGramModel::Create(model_->low_confidence_ngram_model(),
                                      feature_processor_ == nullptr
                                          ? nullptr
                                          : feature_processor_->tokenizer(),
                                      unilib_);
    if (ngram_model_ == nullptr) {
      TC3_LOG(ERROR) << "Could not create ngram linear regression model.";
      return false;
    }
  }

  return true;
}

bool ActionsSuggestions::InitializeTriggeringPreconditions() {
  triggering_preconditions_overlay_ =
      LoadAndVerifyFlatbuffer<TriggeringPreconditions>(
          triggering_preconditions_overlay_buffer_);

  if (triggering_preconditions_overlay_ == nullptr &&
      !triggering_preconditions_overlay_buffer_.empty()) {
    TC3_LOG(ERROR) << "Could not load triggering preconditions overwrites.";
    return false;
  }
  const flatbuffers::Table* overlay =
      reinterpret_cast<const flatbuffers::Table*>(
          triggering_preconditions_overlay_);
  const TriggeringPreconditions* defaults = model_->preconditions();
  if (defaults == nullptr) {
    TC3_LOG(ERROR) << "No triggering conditions specified.";
    return false;
  }

  preconditions_.min_smart_reply_triggering_score = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_MIN_SMART_REPLY_TRIGGERING_SCORE,
      defaults->min_smart_reply_triggering_score());
  preconditions_.max_sensitive_topic_score = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_MAX_SENSITIVE_TOPIC_SCORE,
      defaults->max_sensitive_topic_score());
  preconditions_.suppress_on_sensitive_topic = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_SUPPRESS_ON_SENSITIVE_TOPIC,
      defaults->suppress_on_sensitive_topic());
  preconditions_.min_input_length =
      ValueOrDefault(overlay, TriggeringPreconditions::VT_MIN_INPUT_LENGTH,
                     defaults->min_input_length());
  preconditions_.max_input_length =
      ValueOrDefault(overlay, TriggeringPreconditions::VT_MAX_INPUT_LENGTH,
                     defaults->max_input_length());
  preconditions_.min_locale_match_fraction = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_MIN_LOCALE_MATCH_FRACTION,
      defaults->min_locale_match_fraction());
  preconditions_.handle_missing_locale_as_supported = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_HANDLE_MISSING_LOCALE_AS_SUPPORTED,
      defaults->handle_missing_locale_as_supported());
  preconditions_.handle_unknown_locale_as_supported = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_HANDLE_UNKNOWN_LOCALE_AS_SUPPORTED,
      defaults->handle_unknown_locale_as_supported());
  preconditions_.suppress_on_low_confidence_input = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_SUPPRESS_ON_LOW_CONFIDENCE_INPUT,
      defaults->suppress_on_low_confidence_input());
  preconditions_.diversification_distance_threshold = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_DIVERSIFICATION_DISTANCE_THRESHOLD,
      defaults->diversification_distance_threshold());
  preconditions_.confidence_threshold =
      ValueOrDefault(overlay, TriggeringPreconditions::VT_CONFIDENCE_THRESHOLD,
                     defaults->confidence_threshold());
  preconditions_.empirical_probability_factor = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_EMPIRICAL_PROBABILITY_FACTOR,
      defaults->empirical_probability_factor());
  preconditions_.min_reply_score_threshold = ValueOrDefault(
      overlay, TriggeringPreconditions::VT_MIN_REPLY_SCORE_THRESHOLD,
      defaults->min_reply_score_threshold());

  return true;
}

bool ActionsSuggestions::InitializeRules(ZlibDecompressor* decompressor) {
  if (model_->rules() != nullptr) {
    if (!InitializeRules(decompressor, model_->rules(), &rules_)) {
      TC3_LOG(ERROR) << "Could not initialize action rules.";
      return false;
    }
  }

  if (model_->low_confidence_rules() != nullptr) {
    if (!InitializeRules(decompressor, model_->low_confidence_rules(),
                         &low_confidence_rules_)) {
      TC3_LOG(ERROR) << "Could not initialize low confidence rules.";
      return false;
    }
  }

  // Extend by rules provided by the overwrite.
  // NOTE: The rules from the original models are *not* cleared.
  if (triggering_preconditions_overlay_ != nullptr &&
      triggering_preconditions_overlay_->low_confidence_rules() != nullptr) {
    // These rules are optionally compressed, but separately.
    std::unique_ptr<ZlibDecompressor> overwrite_decompressor =
        ZlibDecompressor::Instance();
    if (overwrite_decompressor == nullptr) {
      TC3_LOG(ERROR) << "Could not initialze decompressor for overwrite rules.";
      return false;
    }
    if (!InitializeRules(
            overwrite_decompressor.get(),
            triggering_preconditions_overlay_->low_confidence_rules(),
            &low_confidence_rules_)) {
      TC3_LOG(ERROR)
          << "Could not initialize low confidence rules from overwrite.";
      return false;
    }
  }

  return true;
}

bool ActionsSuggestions::InitializeRules(
    ZlibDecompressor* decompressor, const RulesModel* rules,
    std::vector<CompiledRule>* compiled_rules) const {
  for (const RulesModel_::Rule* rule : *rules->rule()) {
    std::unique_ptr<UniLib::RegexPattern> compiled_pattern =
        UncompressMakeRegexPattern(
            *unilib_, rule->pattern(), rule->compressed_pattern(),
            rules->lazy_regex_compilation(), decompressor);
    if (compiled_pattern == nullptr) {
      TC3_LOG(ERROR) << "Failed to load rule pattern.";
      return false;
    }

    // Check whether there is a check on the output.
    std::unique_ptr<UniLib::RegexPattern> compiled_output_pattern;
    if (rule->output_pattern() != nullptr ||
        rule->compressed_output_pattern() != nullptr) {
      compiled_output_pattern = UncompressMakeRegexPattern(
          *unilib_, rule->output_pattern(), rule->compressed_output_pattern(),
          rules->lazy_regex_compilation(), decompressor);
      if (compiled_output_pattern == nullptr) {
        TC3_LOG(ERROR) << "Failed to load rule output pattern.";
        return false;
      }
    }

    compiled_rules->emplace_back(rule, std::move(compiled_pattern),
                                 std::move(compiled_output_pattern));
  }

  return true;
}

bool ActionsSuggestions::IsLowConfidenceInput(
    const Conversation& conversation, const int num_messages,
    std::vector<int>* post_check_rules) const {
  for (int i = 1; i <= num_messages; i++) {
    const std::string& message =
        conversation.messages[conversation.messages.size() - i].text;
    const UnicodeText message_unicode(
        UTF8ToUnicodeText(message, /*do_copy=*/false));

    // Run ngram linear regression model.
    if (ngram_model_ != nullptr) {
      if (ngram_model_->Eval(message_unicode)) {
        return true;
      }
    }

    // Run the regex based rules.
    for (int low_confidence_rule = 0;
         low_confidence_rule < low_confidence_rules_.size();
         low_confidence_rule++) {
      const CompiledRule& rule = low_confidence_rules_[low_confidence_rule];
      const std::unique_ptr<UniLib::RegexMatcher> matcher =
          rule.pattern->Matcher(message_unicode);
      int status = UniLib::RegexMatcher::kNoError;
      if (matcher->Find(&status) && status == UniLib::RegexMatcher::kNoError) {
        // Rule only applies to input-output pairs, so defer the check.
        if (rule.output_pattern != nullptr) {
          post_check_rules->push_back(low_confidence_rule);
          continue;
        }
        return true;
      }
    }
  }
  return false;
}

bool ActionsSuggestions::FilterConfidenceOutput(
    const std::vector<int>& post_check_rules,
    std::vector<ActionSuggestion>* actions) const {
  if (post_check_rules.empty() || actions->empty()) {
    return true;
  }
  std::vector<ActionSuggestion> filtered_text_replies;
  for (const ActionSuggestion& action : *actions) {
    if (action.response_text.empty()) {
      filtered_text_replies.push_back(action);
      continue;
    }
    bool passes_post_check = true;
    const UnicodeText text_reply_unicode(
        UTF8ToUnicodeText(action.response_text, /*do_copy=*/false));
    for (const int rule_id : post_check_rules) {
      const std::unique_ptr<UniLib::RegexMatcher> matcher =
          low_confidence_rules_[rule_id].output_pattern->Matcher(
              text_reply_unicode);
      if (matcher == nullptr) {
        TC3_LOG(ERROR) << "Could not create matcher for post check rule.";
        return false;
      }
      int status = UniLib::RegexMatcher::kNoError;
      if (matcher->Find(&status) || status != UniLib::RegexMatcher::kNoError) {
        passes_post_check = false;
        break;
      }
    }
    if (passes_post_check) {
      filtered_text_replies.push_back(action);
    }
  }
  *actions = std::move(filtered_text_replies);
  return true;
}

ActionSuggestion ActionsSuggestions::SuggestionFromSpec(
    const ActionSuggestionSpec* action, const std::string& default_type,
    const std::string& default_response_text,
    const std::string& default_serialized_entity_data,
    const float default_score, const float default_priority_score) const {
  ActionSuggestion suggestion;
  suggestion.score = action != nullptr ? action->score() : default_score;
  suggestion.priority_score =
      action != nullptr ? action->priority_score() : default_priority_score;
  suggestion.type = action != nullptr && action->type() != nullptr
                        ? action->type()->str()
                        : default_type;
  suggestion.response_text =
      action != nullptr && action->response_text() != nullptr
          ? action->response_text()->str()
          : default_response_text;
  suggestion.serialized_entity_data =
      action != nullptr && action->serialized_entity_data() != nullptr
          ? action->serialized_entity_data()->str()
          : default_serialized_entity_data;
  return suggestion;
}

bool ActionsSuggestions::ExtractTokenFeatures(
    const std::vector<std::string>& context, std::vector<float>* embeddings,
    std::vector<int>* num_tokens_per_message,
    int* max_num_tokens_per_message) const {
  if (feature_processor_ == nullptr) {
    TC3_LOG(ERROR) << "Missing token feature extractor.";
    return false;
  }

  // Tokenize and extract features from the input text.
  const int num_messages = context.size();
  std::vector<std::vector<Token>> tokens(num_messages);
  num_tokens_per_message->resize(num_messages);
  *max_num_tokens_per_message = 0;
  for (int i = 0; i < num_messages; i++) {
    tokens[i] = feature_processor_->tokenizer()->Tokenize(context[i]);
    const int num_message_tokens = tokens[i].size();
    (*num_tokens_per_message)[i] = num_message_tokens;
    if (num_message_tokens > *max_num_tokens_per_message) {
      *max_num_tokens_per_message = num_message_tokens;
    }
  }

  // Overwrite the number of tokens if specified.
  if (model_->feature_processor_options()->num_tokens_per_message() > 0) {
    *max_num_tokens_per_message =
        model_->feature_processor_options()->num_tokens_per_message();
  }

  if (*max_num_tokens_per_message <= 0) {
    TC3_LOG(ERROR) << "Could not tokenize input.";
    return false;
  }

  // Embed all tokens and add paddings to pad tokens of each message to the
  // maximum number of tokens in a message of the conversation.
  // If a number of tokens is specified in the model config, tokens at the
  // beginning of a message are dropped if they don't fit in the limit.
  for (int i = 0; i < num_messages; i++) {
    const int start =
        std::max<int>(tokens[i].size() - *max_num_tokens_per_message, 0);
    for (int pos = start; pos < tokens[i].size(); pos++) {
      if (!feature_processor_->AppendTokenFeatures(
              tokens[i][pos], embedding_executor_.get(), embeddings)) {
        TC3_LOG(ERROR) << "Could not run token feature extractor.";
        return false;
      }
    }
    // Add padding.
    for (int k = tokens[i].size(); k < *max_num_tokens_per_message; k++) {
      embeddings->insert(embeddings->end(), embedded_padding_token_.begin(),
                         embedded_padding_token_.end());
    }
  }

  return true;
}

bool ActionsSuggestions::AllocateInput(const int conversation_length,
                                       const int max_tokens,
                                       tflite::Interpreter* interpreter) const {
  if (model_->tflite_model_spec()->resize_inputs()) {
    if (model_->tflite_model_spec()->input_context() >= 0) {
      interpreter->ResizeInputTensor(
          interpreter->inputs()[model_->tflite_model_spec()->input_context()],
          {1, conversation_length});
    }
    if (model_->tflite_model_spec()->input_user_id() >= 0) {
      interpreter->ResizeInputTensor(
          interpreter->inputs()[model_->tflite_model_spec()->input_user_id()],
          {1, conversation_length});
    }
    if (model_->tflite_model_spec()->input_time_diffs() >= 0) {
      interpreter->ResizeInputTensor(
          interpreter
              ->inputs()[model_->tflite_model_spec()->input_time_diffs()],
          {1, conversation_length});
    }
    if (model_->tflite_model_spec()->input_num_tokens() >= 0) {
      interpreter->ResizeInputTensor(
          interpreter
              ->inputs()[model_->tflite_model_spec()->input_num_tokens()],
          {conversation_length, max_tokens});
    }
    if (model_->tflite_model_spec()->input_token_embeddings() >= 0) {
      interpreter->ResizeInputTensor(
          interpreter
              ->inputs()[model_->tflite_model_spec()->input_token_embeddings()],
          {conversation_length, max_tokens, token_embedding_size_});
    }
  }

  return interpreter->AllocateTensors() == kTfLiteOk;
}

bool ActionsSuggestions::SetupModelInput(
    const std::vector<std::string>& context, const std::vector<int>& user_ids,
    const std::vector<float>& time_diffs, const int num_suggestions,
    const float confidence_threshold, const float diversification_distance,
    const float empirical_probability_factor,
    tflite::Interpreter* interpreter) const {
  // Compute token embeddings.
  std::vector<float> token_embeddings;
  std::vector<int> num_tokens_per_message;
  int max_tokens = 0;
  if (model_->tflite_model_spec()->input_num_tokens() >= 0 ||
      model_->tflite_model_spec()->input_token_embeddings() >= 0) {
    if (!ExtractTokenFeatures(context, &token_embeddings,
                              &num_tokens_per_message, &max_tokens)) {
      TC3_LOG(ERROR) << "Could not compute token hashes.";
      return false;
    }
  }

  if (!AllocateInput(context.size(), max_tokens, interpreter)) {
    TC3_LOG(ERROR) << "TensorFlow Lite model allocation failed.";
    return false;
  }
  if (model_->tflite_model_spec()->input_context() >= 0) {
    model_executor_->SetInput<std::string>(
        model_->tflite_model_spec()->input_context(), context, interpreter);
  }
  if (model_->tflite_model_spec()->input_context_length() >= 0) {
    model_executor_->SetInput<int>(
        model_->tflite_model_spec()->input_context_length(), context.size(),
        interpreter);
  }
  if (model_->tflite_model_spec()->input_user_id() >= 0) {
    model_executor_->SetInput<int>(model_->tflite_model_spec()->input_user_id(),
                                   user_ids, interpreter);
  }
  if (model_->tflite_model_spec()->input_num_suggestions() >= 0) {
    model_executor_->SetInput<int>(
        model_->tflite_model_spec()->input_num_suggestions(), num_suggestions,
        interpreter);
  }
  if (model_->tflite_model_spec()->input_time_diffs() >= 0) {
    model_executor_->SetInput<float>(
        model_->tflite_model_spec()->input_time_diffs(), time_diffs,
        interpreter);
  }
  if (model_->tflite_model_spec()->input_diversification_distance() >= 0) {
    model_executor_->SetInput<float>(
        model_->tflite_model_spec()->input_diversification_distance(),
        diversification_distance, interpreter);
  }
  if (model_->tflite_model_spec()->input_confidence_threshold() >= 0) {
    model_executor_->SetInput<float>(
        model_->tflite_model_spec()->input_confidence_threshold(),
        confidence_threshold, interpreter);
  }
  if (model_->tflite_model_spec()->input_empirical_probability_factor() >= 0) {
    model_executor_->SetInput<float>(
        model_->tflite_model_spec()->input_empirical_probability_factor(),
        confidence_threshold, interpreter);
  }
  if (model_->tflite_model_spec()->input_num_tokens() >= 0) {
    model_executor_->SetInput<int>(
        model_->tflite_model_spec()->input_num_tokens(), num_tokens_per_message,
        interpreter);
  }
  if (model_->tflite_model_spec()->input_token_embeddings() >= 0) {
    model_executor_->SetInput<float>(
        model_->tflite_model_spec()->input_token_embeddings(), token_embeddings,
        interpreter);
  }
  return true;
}

bool ActionsSuggestions::ReadModelOutput(
    tflite::Interpreter* interpreter, const ActionSuggestionOptions& options,
    ActionsSuggestionsResponse* response) const {
  // Read sensitivity and triggering score predictions.
  if (model_->tflite_model_spec()->output_triggering_score() >= 0) {
    const TensorView<float>& triggering_score =
        model_executor_->OutputView<float>(
            model_->tflite_model_spec()->output_triggering_score(),
            interpreter);
    if (!triggering_score.is_valid() || triggering_score.size() == 0) {
      TC3_LOG(ERROR) << "Could not compute triggering score.";
      return false;
    }
    response->triggering_score = triggering_score.data()[0];
    response->output_filtered_min_triggering_score =
        (response->triggering_score <
         preconditions_.min_smart_reply_triggering_score);
  }
  if (model_->tflite_model_spec()->output_sensitive_topic_score() >= 0) {
    const TensorView<float>& sensitive_topic_score =
        model_executor_->OutputView<float>(
            model_->tflite_model_spec()->output_sensitive_topic_score(),
            interpreter);
    if (!sensitive_topic_score.is_valid() ||
        sensitive_topic_score.dim(0) != 1) {
      TC3_LOG(ERROR) << "Could not compute sensitive topic score.";
      return false;
    }
    response->sensitivity_score = sensitive_topic_score.data()[0];
    response->output_filtered_sensitivity =
        (response->sensitivity_score >
         preconditions_.max_sensitive_topic_score);
  }

  // Suppress model outputs.
  if (response->output_filtered_sensitivity) {
    return true;
  }

  // Read smart reply predictions.
  std::vector<ActionSuggestion> text_replies;
  if (!response->output_filtered_min_triggering_score &&
      model_->tflite_model_spec()->output_replies() >= 0) {
    const std::vector<tflite::StringRef> replies =
        model_executor_->Output<tflite::StringRef>(
            model_->tflite_model_spec()->output_replies(), interpreter);
    TensorView<float> scores = model_executor_->OutputView<float>(
        model_->tflite_model_spec()->output_replies_scores(), interpreter);
    for (int i = 0; i < replies.size(); i++) {
      if (replies[i].len == 0) continue;
      const float score = scores.data()[i];
      if (score < preconditions_.min_reply_score_threshold) {
        continue;
      }
      response->actions.push_back({std::string(replies[i].str, replies[i].len),
                                   model_->smart_reply_action_type()->str(),
                                   score});
    }
  }

  // Read actions suggestions.
  if (model_->tflite_model_spec()->output_actions_scores() >= 0) {
    const TensorView<float> actions_scores = model_executor_->OutputView<float>(
        model_->tflite_model_spec()->output_actions_scores(), interpreter);
    for (int i = 0; i < model_->action_type()->Length(); i++) {
      const ActionTypeOptions* action_type = model_->action_type()->Get(i);
      // Skip disabled action classes, such as the default other category.
      if (!action_type->enabled()) {
        continue;
      }
      const float score = actions_scores.data()[i];
      if (score < action_type->min_triggering_score()) {
        continue;
      }
      ActionSuggestion suggestion =
          SuggestionFromSpec(action_type->action(),
                             /*default_type=*/action_type->name()->str());
      suggestion.score = score;
      response->actions.push_back(suggestion);
    }
  }

  return true;
}

bool ActionsSuggestions::SuggestActionsFromModel(
    const Conversation& conversation, const int num_messages,
    const ActionSuggestionOptions& options,
    ActionsSuggestionsResponse* response,
    std::unique_ptr<tflite::Interpreter>* interpreter) const {
  TC3_CHECK_LE(num_messages, conversation.messages.size());

  if (!model_executor_) {
    return true;
  }
  *interpreter = model_executor_->CreateInterpreter();

  if (!*interpreter) {
    TC3_LOG(ERROR) << "Could not build TensorFlow Lite interpreter for the "
                      "actions suggestions model.";
    return false;
  }

  std::vector<std::string> context;
  std::vector<int> user_ids;
  std::vector<float> time_diffs;

  // Gather last `num_messages` messages from the conversation.
  int64 last_message_reference_time_ms_utc = 0;
  const float second_in_ms = 1000;
  for (int i = conversation.messages.size() - num_messages;
       i < conversation.messages.size(); i++) {
    const ConversationMessage& message = conversation.messages[i];
    context.push_back(message.text);
    user_ids.push_back(message.user_id);

    float time_diff_secs = 0;
    if (message.reference_time_ms_utc != 0 &&
        last_message_reference_time_ms_utc != 0) {
      time_diff_secs = std::max(0.0f, (message.reference_time_ms_utc -
                                       last_message_reference_time_ms_utc) /
                                          second_in_ms);
    }
    if (message.reference_time_ms_utc != 0) {
      last_message_reference_time_ms_utc = message.reference_time_ms_utc;
    }
    time_diffs.push_back(time_diff_secs);
  }

  if (!SetupModelInput(context, user_ids, time_diffs,
                       /*num_suggestions=*/model_->num_smart_replies(),
                       preconditions_.confidence_threshold,
                       preconditions_.diversification_distance_threshold,
                       preconditions_.empirical_probability_factor,
                       interpreter->get())) {
    TC3_LOG(ERROR) << "Failed to setup input for TensorFlow Lite model.";
    return false;
  }

  if ((*interpreter)->Invoke() != kTfLiteOk) {
    TC3_LOG(ERROR) << "Failed to invoke TensorFlow Lite interpreter.";
    return false;
  }

  return ReadModelOutput(interpreter->get(), options, response);
}

AnnotationOptions ActionsSuggestions::AnnotationOptionsForMessage(
    const ConversationMessage& message) const {
  AnnotationOptions options;
  options.detected_text_language_tags = message.detected_text_language_tags;
  options.reference_time_ms_utc = message.reference_time_ms_utc;
  options.reference_timezone = message.reference_timezone;
  options.annotation_usecase =
      model_->annotation_actions_spec()->annotation_usecase();
  return options;
}

void ActionsSuggestions::SuggestActionsFromAnnotations(
    const Conversation& conversation, const ActionSuggestionOptions& options,
    const Annotator* annotator, std::vector<ActionSuggestion>* actions) const {
  if (model_->annotation_actions_spec() == nullptr ||
      model_->annotation_actions_spec()->annotation_mapping() == nullptr ||
      model_->annotation_actions_spec()->annotation_mapping()->size() == 0) {
    return;
  }

  // Create actions based on the annotations present in the last message.
  const ConversationMessage& message = conversation.messages.back();
  std::vector<AnnotatedSpan> annotations = message.annotations;

  if (annotations.empty() && annotator != nullptr) {
    annotations =
        annotator->Annotate(message.text, AnnotationOptionsForMessage(message));
  }
  const int message_index = conversation.messages.size() - 1;
  std::vector<ActionSuggestionAnnotation> action_annotations;
  action_annotations.reserve(annotations.size());
  for (const AnnotatedSpan& annotation : annotations) {
    if (annotation.classification.empty()) {
      continue;
    }

    const ClassificationResult& classification_result =
        annotation.classification[0];

    ActionSuggestionAnnotation action_annotation;
    action_annotation.span = {
        message_index, annotation.span,
        UTF8ToUnicodeText(message.text, /*do_copy=*/false)
            .UTF8Substring(annotation.span.first, annotation.span.second)};
    action_annotation.entity = classification_result;
    action_annotation.name = classification_result.collection;
    action_annotations.push_back(action_annotation);
  }

  if (model_->annotation_actions_spec()->deduplicate_annotations()) {
    // Create actions only for deduplicated annotations.
    for (const int annotation_id : DeduplicateAnnotations(action_annotations)) {
      SuggestActionsFromAnnotation(message_index,
                                   action_annotations[annotation_id], actions);
    }
  } else {
    // Create actions for all annotations.
    for (const ActionSuggestionAnnotation& annotation : action_annotations) {
      SuggestActionsFromAnnotation(message_index, annotation, actions);
    }
  }
}

void ActionsSuggestions::SuggestActionsFromAnnotation(
    const int message_index, const ActionSuggestionAnnotation& annotation,
    std::vector<ActionSuggestion>* actions) const {
  for (const AnnotationActionsSpec_::AnnotationMapping* mapping :
       *model_->annotation_actions_spec()->annotation_mapping()) {
    if (annotation.entity.collection ==
        mapping->annotation_collection()->str()) {
      if (annotation.entity.score < mapping->min_annotation_score()) {
        continue;
      }
      ActionSuggestion suggestion = SuggestionFromSpec(mapping->action());
      if (mapping->use_annotation_score()) {
        suggestion.score = annotation.entity.score;
      }

      // Set annotation text as (additional) entity data field.
      if (mapping->entity_field() != nullptr) {
        std::unique_ptr<ReflectiveFlatbuffer> entity_data =
            entity_data_builder_->NewRoot();
        TC3_CHECK(entity_data != nullptr);

        // Merge existing static entity data.
        if (!suggestion.serialized_entity_data.empty()) {
          entity_data->MergeFromSerializedFlatbuffer(
              StringPiece(suggestion.serialized_entity_data.c_str(),
                          suggestion.serialized_entity_data.size()));
        }

        entity_data->ParseAndSet(mapping->entity_field(), annotation.span.text);
        suggestion.serialized_entity_data = entity_data->Serialize();
      }

      suggestion.annotations = {annotation};
      actions->push_back(suggestion);
    }
  }
}

std::vector<int> ActionsSuggestions::DeduplicateAnnotations(
    const std::vector<ActionSuggestionAnnotation>& annotations) const {
  std::map<std::pair<std::string, std::string>, int> deduplicated_annotations;

  for (int i = 0; i < annotations.size(); i++) {
    const std::pair<std::string, std::string> key = {annotations[i].name,
                                                     annotations[i].span.text};
    auto entry = deduplicated_annotations.find(key);
    if (entry != deduplicated_annotations.end()) {
      // Kepp the annotation with the higher score.
      if (annotations[entry->second].entity.score <
          annotations[i].entity.score) {
        entry->second = i;
      }
      continue;
    }
    deduplicated_annotations.insert(entry, {key, i});
  }

  std::vector<int> result;
  result.reserve(deduplicated_annotations.size());
  for (const auto& key_and_annotation : deduplicated_annotations) {
    result.push_back(key_and_annotation.second);
  }
  return result;
}

bool ActionsSuggestions::HasEntityData(const RulesModel_::Rule* rule) const {
  for (const RulesModel_::Rule_::RuleActionSpec* rule_action :
       *rule->actions()) {
    if (rule_action->action()->serialized_entity_data() != nullptr ||
        rule_action->capturing_group() != nullptr) {
      return true;
    }
  }
  return false;
}

bool ActionsSuggestions::FillAnnotationFromMatchGroup(
    const UniLib::RegexMatcher* matcher,
    const RulesModel_::Rule_::RuleActionSpec_::CapturingGroup* group,
    const int message_index, ActionSuggestionAnnotation* annotation) const {
  if (group->annotation_name() != nullptr ||
      group->annotation_type() != nullptr) {
    int status = UniLib::RegexMatcher::kNoError;
    const CodepointSpan span = {matcher->Start(group->group_id(), &status),
                                matcher->End(group->group_id(), &status)};
    std::string text =
        matcher->Group(group->group_id(), &status).ToUTF8String();
    if (status != UniLib::RegexMatcher::kNoError) {
      TC3_LOG(ERROR) << "Could not extract span from rule capturing group.";
      return false;
    }

    // The capturing group was not part of the match.
    if (span.first == kInvalidIndex || span.second == kInvalidIndex) {
      return false;
    }
    annotation->span.span = span;
    annotation->span.message_index = message_index;
    annotation->span.text = text;
    if (group->annotation_name() != nullptr) {
      annotation->name = group->annotation_name()->str();
    }
    if (group->annotation_type() != nullptr) {
      annotation->entity.collection = group->annotation_type()->str();
    }
  }
  return true;
}

bool ActionsSuggestions::SuggestActionsFromRules(
    const Conversation& conversation,
    std::vector<ActionSuggestion>* actions) const {
  // Create actions based on rules checking the last message.
  const int message_index = conversation.messages.size() - 1;
  const std::string& message = conversation.messages.back().text;
  const UnicodeText message_unicode(
      UTF8ToUnicodeText(message, /*do_copy=*/false));
  for (const CompiledRule& rule : rules_) {
    const std::unique_ptr<UniLib::RegexMatcher> matcher =
        rule.pattern->Matcher(message_unicode);
    int status = UniLib::RegexMatcher::kNoError;
    const bool has_entity_data = HasEntityData(rule.rule);
    while (matcher->Find(&status) && status == UniLib::RegexMatcher::kNoError) {
      for (const RulesModel_::Rule_::RuleActionSpec* rule_action :
           *rule.rule->actions()) {
        const ActionSuggestionSpec* action = rule_action->action();
        std::vector<ActionSuggestionAnnotation> annotations;

        std::string serialized_entity_data;
        if (has_entity_data) {
          TC3_CHECK(entity_data_builder_ != nullptr);
          std::unique_ptr<ReflectiveFlatbuffer> entity_data =
              entity_data_builder_->NewRoot();
          TC3_CHECK(entity_data != nullptr);

          // Set static entity data.
          if (action->serialized_entity_data() != nullptr) {
            entity_data->MergeFromSerializedFlatbuffer(
                StringPiece(action->serialized_entity_data()->c_str(),
                            action->serialized_entity_data()->size()));
          }

          // Add entity data from rule capturing groups.
          if (rule_action->capturing_group() != nullptr) {
            for (const RulesModel_::Rule_::RuleActionSpec_::CapturingGroup*
                     group : *rule_action->capturing_group()) {
              if (group->entity_field() != nullptr &&
                  !SetFieldFromCapturingGroup(
                      group->group_id(), group->entity_field(), matcher.get(),
                      entity_data.get())) {
                TC3_LOG(ERROR)
                    << "Could not set entity data from rule capturing group.";
                return false;
              }

              // Create a text annotation for the group span.
              ActionSuggestionAnnotation annotation;
              if (FillAnnotationFromMatchGroup(matcher.get(), group,
                                               message_index, &annotation)) {
                annotations.push_back(annotation);
              }
            }
          }

          serialized_entity_data = entity_data->Serialize();
        }
        ActionSuggestion suggestion = SuggestionFromSpec(action);
        suggestion.annotations = annotations;
        suggestion.serialized_entity_data = serialized_entity_data;
        actions->push_back(suggestion);
      }
    }
  }
  return true;
}

bool ActionsSuggestions::SuggestActionsFromLua(
    const Conversation& conversation, const TfLiteModelExecutor* model_executor,
    const tflite::Interpreter* interpreter,
    const reflection::Schema* annotation_entity_data_schema,
    std::vector<ActionSuggestion>* actions) const {
  if (lua_bytecode_.empty()) {
    return true;
  }

  auto lua_actions = LuaActionsSuggestions::CreateLuaActionsSuggestions(
      lua_bytecode_, conversation, model_executor, model_->tflite_model_spec(),
      interpreter, entity_data_schema_, annotation_entity_data_schema);
  if (lua_actions == nullptr) {
    TC3_LOG(ERROR) << "Could not create lua actions.";
    return false;
  }
  return lua_actions->SuggestActions(actions);
}

bool ActionsSuggestions::GatherActionsSuggestions(
    const Conversation& conversation, const Annotator* annotator,
    const ActionSuggestionOptions& options,
    ActionsSuggestionsResponse* response) const {
  if (conversation.messages.empty()) {
    return true;
  }

  const int conversation_history_length = conversation.messages.size();
  const int max_conversation_history_length =
      model_->max_conversation_history_length();
  const int num_messages =
      ((max_conversation_history_length < 0 ||
        conversation_history_length < max_conversation_history_length)
           ? conversation_history_length
           : max_conversation_history_length);

  if (num_messages <= 0) {
    TC3_LOG(INFO) << "No messages provided for actions suggestions.";
    return false;
  }

  SuggestActionsFromAnnotations(conversation, options, annotator,
                                &response->actions);

  int input_text_length = 0;
  int num_matching_locales = 0;
  for (int i = conversation.messages.size() - num_messages;
       i < conversation.messages.size(); i++) {
    input_text_length += conversation.messages[i].text.length();
    std::vector<Locale> message_languages;
    if (!ParseLocales(conversation.messages[i].detected_text_language_tags,
                      &message_languages)) {
      continue;
    }
    if (Locale::IsAnyLocaleSupported(
            message_languages, locales_,
            preconditions_.handle_unknown_locale_as_supported)) {
      ++num_matching_locales;
    }
  }

  // Bail out if we are provided with too few or too much input.
  if (input_text_length < preconditions_.min_input_length ||
      (preconditions_.max_input_length >= 0 &&
       input_text_length > preconditions_.max_input_length)) {
    TC3_LOG(INFO) << "Too much or not enough input for inference.";
    return response;
  }

  // Bail out if the text does not look like it can be handled by the model.
  const float matching_fraction =
      static_cast<float>(num_matching_locales) / num_messages;
  if (matching_fraction < preconditions_.min_locale_match_fraction) {
    TC3_LOG(INFO) << "Not enough locale matches.";
    response->output_filtered_locale_mismatch = true;
    return true;
  }

  std::vector<int> post_check_rules;
  if (preconditions_.suppress_on_low_confidence_input &&
      IsLowConfidenceInput(conversation, num_messages, &post_check_rules)) {
    response->output_filtered_low_confidence = true;
    return true;
  }

  std::unique_ptr<tflite::Interpreter> interpreter;
  if (!SuggestActionsFromModel(conversation, num_messages, options, response,
                               &interpreter)) {
    TC3_LOG(ERROR) << "Could not run model.";
    return false;
  }

  // Suppress all predictions if the conversation was deemed sensitive.
  if (preconditions_.suppress_on_sensitive_topic &&
      response->output_filtered_sensitivity) {
    return true;
  }

  if (!SuggestActionsFromLua(
          conversation, model_executor_.get(), interpreter.get(),
          annotator != nullptr ? annotator->entity_data_schema() : nullptr,
          &response->actions)) {
    TC3_LOG(ERROR) << "Could not suggest actions from script.";
    return false;
  }

  if (!SuggestActionsFromRules(conversation, &response->actions)) {
    TC3_LOG(ERROR) << "Could not suggest actions from rules.";
    return false;
  }

  if (preconditions_.suppress_on_low_confidence_input &&
      !FilterConfidenceOutput(post_check_rules, &response->actions)) {
    TC3_LOG(ERROR) << "Could not post-check actions.";
    return false;
  }

  return true;
}

ActionsSuggestionsResponse ActionsSuggestions::SuggestActions(
    const Conversation& conversation, const Annotator* annotator,
    const ActionSuggestionOptions& options) const {
  ActionsSuggestionsResponse response;
  if (!GatherActionsSuggestions(conversation, annotator, options, &response)) {
    TC3_LOG(ERROR) << "Could not gather actions suggestions.";
    response.actions.clear();
  } else if (!ranker_->RankActions(&response, entity_data_schema_,
                                   annotator != nullptr
                                       ? annotator->entity_data_schema()
                                       : nullptr)) {
    TC3_LOG(ERROR) << "Could not rank actions.";
    response.actions.clear();
  }
  return response;
}

ActionsSuggestionsResponse ActionsSuggestions::SuggestActions(
    const Conversation& conversation,
    const ActionSuggestionOptions& options) const {
  return SuggestActions(conversation, /*annotator=*/nullptr, options);
}

const ActionsModel* ActionsSuggestions::model() const { return model_; }
const reflection::Schema* ActionsSuggestions::entity_data_schema() const {
  return entity_data_schema_;
}

const ActionsModel* ViewActionsModel(const void* buffer, int size) {
  if (buffer == nullptr) {
    return nullptr;
  }
  return LoadAndVerifyModel(reinterpret_cast<const uint8_t*>(buffer), size);
}

}  // namespace libtextclassifier3
