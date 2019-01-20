FLATC := $(HOST_OUT_EXECUTABLES)/flatc$(HOST_EXECUTABLE_SUFFIX)

define transform-fbs-to-cpp
@echo "Flatc: $@ <= $(PRIVATE_INPUT_FBS)"
@rm -f $@
@mkdir -p $(dir $@)
$(FLATC) \
    --cpp \
    --no-union-value-namespacing \
    --gen-object-api \
    --keep-prefix \
    -I $(INPUT_DIR) \
    -o $(dir $@) \
    $(PRIVATE_INPUT_FBS) \
    || exit 33
[ -f $@ ] || exit 33
endef

intermediates := $(call local-generated-sources-dir)

# Generate utils/zlib/buffer_generated.h using FlatBuffer schema compiler.
UTILS_ZLIB_BUFFER_FBS := $(LOCAL_PATH)/utils/zlib/buffer.fbs
UTILS_ZLIB_BUFFER_H := $(intermediates)/utils/zlib/buffer_generated.h
$(UTILS_ZLIB_BUFFER_H): PRIVATE_INPUT_FBS := $(UTILS_ZLIB_BUFFER_FBS)
$(UTILS_ZLIB_BUFFER_H): INPUT_DIR := $(LOCAL_PATH)
$(UTILS_ZLIB_BUFFER_H): $(FLATC) $(UTILS_ZLIB_BUFFER_FBS)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(UTILS_ZLIB_BUFFER_H)

# Generate utils/intent/intent-config_generated.h using FlatBuffer schema compiler.
INTENT_CONFIG_FBS := $(LOCAL_PATH)/utils/intents/intent-config.fbs
INTENT_CONFIG_H := $(intermediates)/utils/intents/intent-config_generated.h
$(INTENT_CONFIG_H): PRIVATE_INPUT_FBS := $(INTENT_CONFIG_FBS)
$(INTENT_CONFIG_H): INPUT_DIR := $(LOCAL_PATH)
$(INTENT_CONFIG_H): $(FLATC) $(INTENT_CONFIG_FBS)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(INTENT_CONFIG_H)

# Generate annotator/model_generated.h using FlatBuffer schema compiler.
ANNOTATOR_MODEL_FBS := $(LOCAL_PATH)/annotator/model.fbs
ANNOTATOR_MODEL_H := $(intermediates)/annotator/model_generated.h
$(ANNOTATOR_MODEL_H): PRIVATE_INPUT_FBS := $(ANNOTATOR_MODEL_FBS)
$(ANNOTATOR_MODEL_H): INPUT_DIR := $(LOCAL_PATH)
$(ANNOTATOR_MODEL_H): $(FLATC) $(ANNOTATOR_MODEL_FBS) $(INTENT_CONFIG_H)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(ANNOTATOR_MODEL_H)

# Generate actions/actions_model_generated.h using FlatBuffer schema compiler.
ACTIONS_MODEL_FBS := $(LOCAL_PATH)/actions/actions_model.fbs
ACTIONS_MODEL_H := $(intermediates)/actions/actions_model_generated.h
$(ACTIONS_MODEL_H): PRIVATE_INPUT_FBS := $(ACTIONS_MODEL_FBS)
$(ACTIONS_MODEL_H): INPUT_DIR := $(LOCAL_PATH)
$(ACTIONS_MODEL_H): $(FLATC) $(ACTIONS_MODEL_FBS)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(ACTIONS_MODEL_H)

# Generate utils/tflite/text_encoder_config_generated.h using FlatBuffer schema compiler.
UTILS_TFLITE_TEXT_ENCODER_CONFIG_FBS := $(LOCAL_PATH)/utils/tflite/text_encoder_config.fbs
UTILS_TFLITE_TEXT_ENCODER_CONFIG_H := $(intermediates)/utils/tflite/text_encoder_config_generated.h
$(UTILS_TFLITE_TEXT_ENCODER_CONFIG_H): PRIVATE_INPUT_FBS := $(UTILS_TFLITE_TEXT_ENCODER_CONFIG_FBS)
$(UTILS_TFLITE_TEXT_ENCODER_CONFIG_H): INPUT_DIR := $(LOCAL_PATH)
$(UTILS_TFLITE_TEXT_ENCODER_CONFIG_H): $(FLATC) $(UTILS_TFLITE_TEXT_ENCODER_CONFIG_FBS)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(UTILS_TFLITE_TEXT_ENCODER_CONFIG_H)

# Generate lang_id/common/flatbuffers/embedding-network_generated.h using FlatBuffer schema compiler.
LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_FBS := $(LOCAL_PATH)/lang_id/common/flatbuffers/embedding-network.fbs
LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_H := $(intermediates)/lang_id/common/flatbuffers/embedding-network_generated.h
$(LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_H): PRIVATE_INPUT_FBS := $(LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_FBS)
$(LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_H): INPUT_DIR := $(LOCAL_PATH)
$(LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_H): $(FLATC) $(LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_FBS)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(LANG_ID_COMMON_FLATBUFFERS_EMBEDDING_NETWORK_H)

# Generate lang_id/common/flatbuffers/model_generated.h using FlatBuffer schema compiler.
LANG_ID_COMMON_FLATBUFFERS_MODEL_FBS := $(LOCAL_PATH)/lang_id/common/flatbuffers/model.fbs
LANG_ID_COMMON_FLATBUFFERS_MODEL_H := $(intermediates)/lang_id/common/flatbuffers/model_generated.h
$(LANG_ID_COMMON_FLATBUFFERS_MODEL_H): PRIVATE_INPUT_FBS := $(LANG_ID_COMMON_FLATBUFFERS_MODEL_FBS)
$(LANG_ID_COMMON_FLATBUFFERS_MODEL_H): INPUT_DIR := $(LOCAL_PATH)
$(LANG_ID_COMMON_FLATBUFFERS_MODEL_H): $(FLATC) $(LANG_ID_COMMON_FLATBUFFERS_MODEL_FBS)
	$(transform-fbs-to-cpp)
LOCAL_GENERATED_SOURCES += $(LANG_ID_COMMON_FLATBUFFERS_MODEL_H)
