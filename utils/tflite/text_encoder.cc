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

#include <memory>
#include <vector>

#include "utils/base/logging.h"
#include "utils/sentencepiece/double_array_trie.h"
#include "utils/sentencepiece/encoder.h"
#include "utils/sentencepiece/normalizer.h"
#include "utils/strings/stringpiece.h"
#include "utils/tflite/text_encoder.h"
#include "utils/tflite/text_encoder_config_generated.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/flexbuffers.h"
#include "tensorflow/contrib/lite/kernels/kernel_util.h"
#include "tensorflow/contrib/lite/model.h"
#include "tensorflow/contrib/lite/string_util.h"

namespace libtextclassifier3 {
namespace {

struct TextEncoderOp {
  std::unique_ptr<Normalizer> normalizer;
  std::unique_ptr<Encoder<DoubleArrayTrie>> encoder;
};

// Input parameters for the op.
enum TextEncoderInputs {
  TEXT_ENCODER_INPUT_TEXTS = 0,
  TEXT_ENCODER_INPUT_NUM_TEXTS = 1,
  TEXT_ENCODER_INPUT_MAX_LENGTH = 2,
  TEXT_ENCODER_INPUT_ATTR = 3
};

// Output parameters for the op.
enum SmartReplyModelOutputs {
  TEXT_ENCODER_OUTPUT_ENCODED = 0,
  TEXT_ENCODER_OUTPUT_LENGTHS = 1,
  TEXT_ENCODER_OUTPUT_ATTR = 2,
};

const char kTextEncoderConfigAttr[] = "text_encoder_config";

// Initializes text encoder object from serialized options:
//   The options are a flexbuffers attribute map that contain the op config
//   with the key `text_encoder_config` as `TextEncoderConfig`.
void* Initialize(TfLiteContext* context, const char* buffer, size_t length) {
  const flexbuffers::Map& attr_map =
      flexbuffers::GetRoot(reinterpret_cast<const uint8_t*>(buffer), length)
          .AsMap();
  const flexbuffers::Blob serialized_config =
      attr_map[kTextEncoderConfigAttr].AsBlob();
  const TextEncoderConfig* config =
      flatbuffers::GetRoot<TextEncoderConfig>(serialized_config.data());

  std::unique_ptr<TextEncoderOp> encoder_op(new TextEncoderOp());

  // Create normalizer from options.
  const TrieNode* charsmap_trie_nodes = reinterpret_cast<const TrieNode*>(
      config->normalization_charsmap()->Data());
  const int charsmap_trie_nodes_length =
      config->normalization_charsmap()->Length() / sizeof(TrieNode);
  encoder_op->normalizer.reset(new Normalizer(
      DoubleArrayTrie(charsmap_trie_nodes, charsmap_trie_nodes_length),
      StringPiece(config->normalization_charsmap_values()->data(),
                  config->normalization_charsmap_values()->size()),
      config->add_dummy_prefix(), config->remove_extra_whitespaces(),
      config->escape_whitespaces()));

  const TrieNode* pieces_trie_nodes =
      reinterpret_cast<const TrieNode*>(config->pieces()->Data());
  const int pieces_trie_nodes_length =
      config->pieces()->Length() / sizeof(TrieNode);
  const int num_pieces = config->pieces_scores()->Length();
  encoder_op->encoder.reset(new Encoder<DoubleArrayTrie>(
      DoubleArrayTrie(pieces_trie_nodes, pieces_trie_nodes_length), num_pieces,
      config->pieces_scores()->data(), config->start_code(), config->end_code(),
      config->encoding_offset()));
  return encoder_op.release();
}

void Free(TfLiteContext* context, void* buffer) {
  delete reinterpret_cast<TextEncoderOp*>(buffer);
}

namespace {
TfLiteIntArray* CreateSizeArray(const std::initializer_list<int>& sizes) {
  TfLiteIntArray* array_size = TfLiteIntArrayCreate(sizes.size());
  int index = 0;
  for (const int size : sizes) {
    array_size->data[index++] = size;
  }
  return array_size;
}

// Copies attributes values according to the encoding_offsets of every string.
TfLiteStatus CopyAttribute(const TfLiteTensor& in,
                           const std::vector<int>& encoding_end_offsets,
                           int start_offset, TfLiteContext* context,
                           TfLiteTensor* out) {
  TF_LITE_ENSURE_EQ(context, in.dims->size, 2);
  TF_LITE_ENSURE_EQ(context, in.dims->data[0], 1);
  const int output_size = out->dims->data[1];
  int output_offset = 0;
  for (int value_index = 0;
       value_index < encoding_end_offsets.size() && output_offset < output_size;
       ++value_index) {
    // Calculate how many elements need to be set with this value.
    // The low bound depends on the offset from the beggining. If this is 0, it
    // means that this value it truncated.
    // The upper bound depends on how many elements are in the output tensor.
    const int from_this_element =
        std::min(std::max(0, encoding_end_offsets[value_index] - start_offset -
                                 output_offset),
                 output_size - output_offset);
    if (from_this_element == 0) {
      continue;
    }

    switch (in.type) {
      case kTfLiteInt32: {
        std::fill(out->data.i32 + output_offset,
                  out->data.i32 + output_offset + from_this_element,
                  in.data.i32[value_index]);
      } break;
      case kTfLiteFloat32: {
        std::fill(out->data.f + output_offset,
                  out->data.f + output_offset + from_this_element,
                  in.data.f[value_index]);
      } break;
      default:
        context->ReportError(
            (context), __FILE__ " Not supported attribute type %d", in.type);
        return kTfLiteError;
    }
    output_offset += from_this_element;
  }
  // Do final padding.
  switch (in.type) {
    case kTfLiteInt32: {
      const int32_t value =
          (output_offset > 0) ? out->data.i32[output_offset - 1] : 0;
      std::fill(out->data.i32 + output_offset, out->data.i32 + output_size,
                value);
    } break;
    case kTfLiteFloat32: {
      const float value =
          (output_offset > 0) ? out->data.f[output_offset - 1] : 0;
      std::fill(out->data.f + output_offset, out->data.f + output_size, value);
    } break;
    default: {}
  }
  return kTfLiteOk;
}

TfLiteStatus ResizeOutputTensors(TfLiteContext* context, TfLiteNode* node,
                                 int max_output_length) {
  TfLiteTensor& output_encoded =
      context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ENCODED]];

  TF_LITE_ENSURE_OK(
      context, context->ResizeTensor(context, &output_encoded,
                                     CreateSizeArray({1, max_output_length})));

  const int num_output_attrs = node->outputs->size - TEXT_ENCODER_OUTPUT_ATTR;
  for (int i = 0; i < num_output_attrs; ++i) {
    TfLiteTensor& output =
        context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ATTR + i]];
    TF_LITE_ENSURE_OK(context, context->ResizeTensor(
                                   context, &output,
                                   CreateSizeArray({1, max_output_length})));
  }
  return kTfLiteOk;
}

}  // namespace

TfLiteStatus Prepare(TfLiteContext* context, TfLiteNode* node) {
  // Check that the batch dimension is 1.
  const TfLiteTensor& input_text =
      context->tensors[node->inputs->data[TEXT_ENCODER_INPUT_TEXTS]];
  TF_LITE_ENSURE_EQ(context, input_text.dims->size, 2);
  TF_LITE_ENSURE_EQ(context, input_text.dims->data[0], 1);

  TfLiteTensor& output_lengths =
      context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_LENGTHS]];
  TfLiteTensor& output_encoded =
      context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ENCODED]];

  TF_LITE_ENSURE_OK(context, context->ResizeTensor(context, &output_lengths,
                                                   CreateSizeArray({1})));

  // Check that there are enough outputs for attributes.
  const int num_output_attrs = node->outputs->size - TEXT_ENCODER_OUTPUT_ATTR;
  TF_LITE_ENSURE_EQ(context, node->inputs->size - TEXT_ENCODER_INPUT_ATTR,
                    num_output_attrs);

  // Copy attribute types from input to output tensors.
  for (int i = 0; i < num_output_attrs; ++i) {
    TfLiteTensor& input =
        context->tensors[node->inputs->data[TEXT_ENCODER_INPUT_ATTR + i]];
    TfLiteTensor& output =
        context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ATTR + i]];
    output.type = input.type;
  }

  const TfLiteTensor& output_length =
      context->tensors[node->inputs->data[TEXT_ENCODER_INPUT_MAX_LENGTH]];

  if (tflite::IsConstantTensor(&output_length)) {
    return ResizeOutputTensors(context, node, output_length.data.i64[0]);
  } else {
    tflite::SetTensorToDynamic(&output_encoded);
    for (int i = 0; i < num_output_attrs; ++i) {
      TfLiteTensor& output_attr =
          context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ATTR + i]];
      tflite::SetTensorToDynamic(&output_attr);
    }
  }

  return kTfLiteOk;
}

TfLiteStatus Eval(TfLiteContext* context, TfLiteNode* node) {
  if (node->user_data == nullptr) {
    return kTfLiteError;
  }
  const TextEncoderOp* encoder_op =
      reinterpret_cast<TextEncoderOp*>(node->user_data);
  const TfLiteTensor& input_text =
      context->tensors[node->inputs->data[TEXT_ENCODER_INPUT_TEXTS]];
  const int num_strings = tflite::GetStringCount(&input_text);

  TfLiteTensor& output_encoded =
      context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ENCODED]];
  if (tflite::IsDynamicTensor(&output_encoded)) {
    const TfLiteTensor& output_length =
        context->tensors[node->inputs->data[TEXT_ENCODER_INPUT_MAX_LENGTH]];
    TF_LITE_ENSURE_OK(
        context, ResizeOutputTensors(context, node, output_length.data.i64[0]));
  }

  std::vector<int> encoded_total;
  std::vector<int> encoded_offsets;
  encoded_offsets.reserve(num_strings);

  for (int i = 0; i < num_strings; ++i) {
    const auto& strref = tflite::GetString(&input_text, i);
    const std::vector<int> encoded = encoder_op->encoder->Encode(
        encoder_op->normalizer->Normalize(StringPiece(strref.str, strref.len)));
    encoded_total.insert(encoded_total.end(), encoded.begin(), encoded.end());
    encoded_offsets.push_back(encoded_total.size());
  }
  const int max_output_length = output_encoded.dims->data[1];

  // Copy encoding to output tensor.
  const int start_offset =
      std::max(0, static_cast<int>(encoded_total.size()) - max_output_length);
  int output_offset = 0;
  int32_t* output_buffer = output_encoded.data.i32;
  for (int i = start_offset; i < encoded_total.size(); ++i, ++output_offset) {
    output_buffer[output_offset] = encoded_total[i];
  }

  // Save output encoded length.
  TfLiteTensor& output_lengths =
      context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_LENGTHS]];
  output_lengths.data.i32[0] = output_offset;

  // Do padding.
  for (; output_offset < max_output_length; ++output_offset) {
    output_buffer[output_offset] = encoded_total.back();
  }

  // Process attributes, all checks of sizes and types are done in Prepare.
  const int num_output_attrs = node->outputs->size - TEXT_ENCODER_OUTPUT_ATTR;
  TF_LITE_ENSURE_EQ(context, node->inputs->size - TEXT_ENCODER_INPUT_ATTR,
                    num_output_attrs);
  for (int i = 0; i < num_output_attrs; ++i) {
    TfLiteStatus attr_status = CopyAttribute(
        context->tensors[node->inputs->data[TEXT_ENCODER_INPUT_ATTR + i]],
        encoded_offsets, start_offset, context,
        &context->tensors[node->outputs->data[TEXT_ENCODER_OUTPUT_ATTR + i]]);
    if (attr_status != kTfLiteOk) {
      return attr_status;
    }
  }

  return kTfLiteOk;
}

}  // namespace
}  // namespace libtextclassifier3

namespace tflite {
namespace ops {
namespace custom {

TfLiteRegistration* Register_TEXT_ENCODER() {
  static TfLiteRegistration registration = {
      libtextclassifier3::Initialize, libtextclassifier3::Free,
      libtextclassifier3::Prepare, libtextclassifier3::Eval};
  return &registration;
}

}  // namespace custom
}  // namespace ops
}  // namespace tflite
