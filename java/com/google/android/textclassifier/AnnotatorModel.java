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

import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Java wrapper for Annotator native library interface. This library is used for detecting entities
 * in text.
 *
 * @hide
 */
public final class AnnotatorModel implements AutoCloseable {
  private final AtomicBoolean isClosed = new AtomicBoolean(false);

  static {
    System.loadLibrary("textclassifier");
  }

  // Keep these in sync with the constants defined in AOSP.
  static final String TYPE_UNKNOWN = "";
  static final String TYPE_OTHER = "other";
  static final String TYPE_EMAIL = "email";
  static final String TYPE_PHONE = "phone";
  static final String TYPE_ADDRESS = "address";
  static final String TYPE_URL = "url";
  static final String TYPE_DATE = "date";
  static final String TYPE_DATE_TIME = "datetime";
  static final String TYPE_FLIGHT_NUMBER = "flight";

  private long annotatorPtr;

  /**
   * Creates a new instance of SmartSelect predictor, using the provided model image, given as a
   * file descriptor.
   */
  public AnnotatorModel(int fileDescriptor) {
    annotatorPtr = nativeNewAnnotator(fileDescriptor);
    if (annotatorPtr == 0L) {
      throw new IllegalArgumentException("Couldn't initialize TC from file descriptor.");
    }
  }

  /**
   * Creates a new instance of SmartSelect predictor, using the provided model image, given as a
   * file path.
   */
  public AnnotatorModel(String path) {
    annotatorPtr = nativeNewAnnotatorFromPath(path);
    if (annotatorPtr == 0L) {
      throw new IllegalArgumentException("Couldn't initialize TC from given file.");
    }
  }

  /** Initializes the knowledge engine, passing the given serialized config to it. */
  public void initializeKnowledgeEngine(byte[] serializedConfig) {
    if (!nativeInitializeKnowledgeEngine(annotatorPtr, serializedConfig)) {
      throw new IllegalArgumentException("Couldn't initialize the KG engine");
    }
  }

  /**
   * Given a string context and current selection, computes the selection suggestion.
   *
   * <p>The begin and end are character indices into the context UTF8 string. selectionBegin is the
   * character index where the selection begins, and selectionEnd is the index of one character past
   * the selection span.
   *
   * <p>The return value is an array of two ints: suggested selection beginning and end, with the
   * same semantics as the input selectionBeginning and selectionEnd.
   */
  public int[] suggestSelection(
      String context, int selectionBegin, int selectionEnd, SelectionOptions options) {
    return nativeSuggestSelection(annotatorPtr, context, selectionBegin, selectionEnd, options);
  }

  /**
   * Given a string context and current selection, classifies the type of the selected text.
   *
   * <p>The begin and end params are character indices in the context string.
   *
   * <p>Returns an array of ClassificationResult objects with the probability scores for different
   * collections.
   */
  public ClassificationResult[] classifyText(
      String context, int selectionBegin, int selectionEnd, ClassificationOptions options) {
    return nativeClassifyText(annotatorPtr, context, selectionBegin, selectionEnd, options);
  }

  /**
   * Annotates given input text. The annotations should cover the whole input context except for
   * whitespaces, and are sorted by their position in the context string.
   */
  public AnnotatedSpan[] annotate(String text, AnnotationOptions options) {
    return nativeAnnotate(annotatorPtr, text, options);
  }

  /** Frees up the allocated memory. */
  @Override
  public void close() {
    if (isClosed.compareAndSet(false, true)) {
      nativeCloseAnnotator(annotatorPtr);
      annotatorPtr = 0L;
    }
  }

  @Override
  protected void finalize() throws Throwable {
    try {
      close();
    } finally {
      super.finalize();
    }
  }

  /** Returns a comma separated list of locales supported by the model as BCP 47 tags. */
  public static String getLocales(int fd) {
    return nativeGetLocales(fd);
  }

  /** Returns the version of the model. */
  public static int getVersion(int fd) {
    return nativeGetVersion(fd);
  }

  /** Returns the name of the model. */
  public static String getName(int fd) {
    return nativeGetName(fd);
  }

  /** Information about a parsed time/date. */
  public static final class DatetimeResult {

    static final int GRANULARITY_YEAR = 0;
    static final int GRANULARITY_MONTH = 1;
    static final int GRANULARITY_WEEK = 2;
    static final int GRANULARITY_DAY = 3;
    static final int GRANULARITY_HOUR = 4;
    static final int GRANULARITY_MINUTE = 5;
    static final int GRANULARITY_SECOND = 6;

    private final long timeMsUtc;
    private final int granularity;

    DatetimeResult(long timeMsUtc, int granularity) {
      this.timeMsUtc = timeMsUtc;
      this.granularity = granularity;
    }

    public long getTimeMsUtc() {
      return timeMsUtc;
    }

    public int getGranularity() {
      return granularity;
    }
  }

  /** Classification result for classifyText method. */
  public static final class ClassificationResult {
    private final String collection;
    private final float score;
    private final DatetimeResult datetimeResult;
    private final byte[] serializedKnowledgeResult;

    public ClassificationResult(
        String collection,
        float score,
        DatetimeResult datetimeResult,
        byte[] serializedKnowledgeResult) {
      this.collection = collection;
      this.score = score;
      this.datetimeResult = datetimeResult;
      this.serializedKnowledgeResult = serializedKnowledgeResult;
    }

    /** Returns the classified entity type. */
    public String getCollection() {
      if (TYPE_DATE.equals(collection) && datetimeResult != null) {
        switch (datetimeResult.getGranularity()) {
          case DatetimeResult.GRANULARITY_HOUR:
          case DatetimeResult.GRANULARITY_MINUTE:
          case DatetimeResult.GRANULARITY_SECOND:
            return TYPE_DATE_TIME;
          default:
            return TYPE_DATE;
        }
      }
      return collection;
    }

    /** Confidence score between 0 and 1. */
    public float getScore() {
      return score;
    }

    public DatetimeResult getDatetimeResult() {
      return datetimeResult;
    }

    byte[] getSerializedKnowledgeResult() {
      return serializedKnowledgeResult;
    }
  }

  /** Represents a result of Annotate call. */
  public static final class AnnotatedSpan {
    private final int startIndex;
    private final int endIndex;
    private final ClassificationResult[] classification;

    AnnotatedSpan(int startIndex, int endIndex, ClassificationResult[] classification) {
      this.startIndex = startIndex;
      this.endIndex = endIndex;
      this.classification = classification;
    }

    public int getStartIndex() {
      return startIndex;
    }

    public int getEndIndex() {
      return endIndex;
    }

    public ClassificationResult[] getClassification() {
      return classification;
    }
  }

  /** Represents options for the suggestSelection call. */
  public static final class SelectionOptions {
    private final String locales;

    public SelectionOptions(String locales) {
      this.locales = locales;
    }

    public String getLocales() {
      return locales;
    }
  }

  /** Represents options for the classifyText call. */
  public static final class ClassificationOptions {
    private final long referenceTimeMsUtc;
    private final String referenceTimezone;
    private final String locales;

    public ClassificationOptions(long referenceTimeMsUtc, String referenceTimezone, String locale) {
      this.referenceTimeMsUtc = referenceTimeMsUtc;
      this.referenceTimezone = referenceTimezone;
      this.locales = locale;
    }

    public long getReferenceTimeMsUtc() {
      return referenceTimeMsUtc;
    }

    public String getReferenceTimezone() {
      return referenceTimezone;
    }

    public String getLocale() {
      return locales;
    }
  }

  /** Represents options for the annotate call. */
  public static final class AnnotationOptions {
    private final long referenceTimeMsUtc;
    private final String referenceTimezone;
    private final String locales;

    public AnnotationOptions(long referenceTimeMsUtc, String referenceTimezone, String locale) {
      this.referenceTimeMsUtc = referenceTimeMsUtc;
      this.referenceTimezone = referenceTimezone;
      this.locales = locale;
    }

    public long getReferenceTimeMsUtc() {
      return referenceTimeMsUtc;
    }

    public String getReferenceTimezone() {
      return referenceTimezone;
    }

    public String getLocale() {
      return locales;
    }
  }

  private static native long nativeNewAnnotator(int fd);

  private static native long nativeNewAnnotatorFromPath(String path);

  private static native String nativeGetLocales(int fd);

  private static native int nativeGetVersion(int fd);

  private static native String nativeGetName(int fd);

  private native boolean nativeInitializeKnowledgeEngine(long context, byte[] serializedConfig);

  private native int[] nativeSuggestSelection(
      long context, String text, int selectionBegin, int selectionEnd, SelectionOptions options);

  private native ClassificationResult[] nativeClassifyText(
      long context,
      String text,
      int selectionBegin,
      int selectionEnd,
      ClassificationOptions options);

  private native AnnotatedSpan[] nativeAnnotate(
      long context, String text, AnnotationOptions options);

  private native void nativeCloseAnnotator(long context);
}
