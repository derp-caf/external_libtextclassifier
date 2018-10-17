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

package com.google.android.textclassifier;

/**
 * Java wrapper for ActionsSuggestions native library interface. This library is used to suggest
 * actions and replies in a given conversation.
 *
 * @hide
 */
public final class ActionsSuggestionsModel implements AutoCloseable {

  static {
    System.loadLibrary("textclassifier");
  }

  private final long actionsModelPtr;

  /**
   * Creates a new instance of Actions predictor, using the provided model image, given as a file
   * descriptor.
   */
  public ActionsSuggestionsModel(int fileDescriptor) {
    actionsModelPtr = nativeNewActionsModel(fileDescriptor);
    if (actionsModelPtr == 0L) {
      throw new IllegalArgumentException("Couldn't initialize actions model from file descriptor.");
    }
  }

  /**
   * Creates a new instance of Actions predictor, using the provided model image, given as a file
   * path.
   */
  public ActionsSuggestionsModel(String path) {
    actionsModelPtr = nativeNewActionsModelFromPath(path);
    if (actionsModelPtr == 0L) {
      throw new IllegalArgumentException("Couldn't initialize actions model from given file.");
    }
  }

  /** Suggests actions / replies to the given conversation. */
  public ActionSuggestion[] suggestActions(
      Conversation conversation, ActionSuggestionOptions options) {
    return nativeSuggestActions(actionsModelPtr, conversation, options);
  }

  /** Frees up the allocated memory. */
  @Override
  public void close() {
    nativeCloseActionsModel(actionsModelPtr);
  }

  /** Action suggestion that contains a response text and the type of the response. */
  public static final class ActionSuggestion {

    private final String responseText;
    private final String actionType;
    private final float score;

    public ActionSuggestion(String responseText, String actionType, float score) {
      this.responseText = responseText;
      this.actionType = actionType;
      this.score = score;
    }

    public String getResponseText() {
      return responseText;
    }

    public String getActionType() {
      return actionType;
    }

    /** Confidence score between 0 and 1 */
    public float getScore() {
      return score;
    }
  }

  /** Represents a single message in the conversation. */
  public static final class ConversationMessage {
    private final int userId;
    private final String text;

    public ConversationMessage(int userId, String text) {
      this.userId = userId;
      this.text = text;
    }

    /** The identifier of the sender */
    public int getUserId() {
      return userId;
    }

    public String getText() {
      return text;
    }
  }

  /** Represents conversation between multiple users. */
  public static final class Conversation {
    public final ConversationMessage[] conversationMessages;

    public Conversation(ConversationMessage[] conversationMessages) {
      this.conversationMessages = conversationMessages;
    }

    public ConversationMessage[] getConversationMessages() {
      return conversationMessages;
    }
  }

  /** Represents options for the SuggestActions call. */
  public static final class ActionSuggestionOptions {}

  private static native long nativeNewActionsModel(int fd);

  private static native long nativeNewActionsModelFromPath(String path);

  private static native ActionSuggestion[] nativeSuggestActions(
      long context, Conversation conversation, ActionSuggestionOptions options);

  private static native void nativeCloseActionsModel(long context);
}
