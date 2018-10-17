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
 * Java wrapper for LangId native library interface. This class is used to detect languages in text.
 *
 * @hide
 */
public final class LangIdModel implements AutoCloseable {

  static {
    System.loadLibrary("textclassifier");
  }

  private final long mModelPtr;

  /** Creates a new instance of LangId predictor, using the provided model image. */
  public LangIdModel(int fd) {
    mModelPtr = nativeNewLangIdModel(fd);
    if (mModelPtr == 0L) {
      throw new IllegalArgumentException("Couldn't initialize LangId from given file descriptor.");
    }
  }

  /** Creates a new instance of LangId predictor, using the provided model image. */
  public LangIdModel(String modelPath) {
    mModelPtr = nativeNewLangIdModelFromPath(modelPath);
    if (mModelPtr == 0L) {
      throw new IllegalArgumentException("Couldn't initialize LangId from given file.");
    }
  }

  /** Detects the languages for given text. */
  public LanguageResult[] detectLanguages(String text) {
    return nativeDetectLanguages(mModelPtr, text);
  }

  /** Frees up the allocated memory. */
  @Override
  public void close() {
    nativeCloseLangIdModel(mModelPtr);
  }

  /** Result for detectLanguages method. */
  public static final class LanguageResult {
    final String mLanguage;
    final float mScore;

    LanguageResult(String language, float score) {
      mLanguage = language;
      mScore = score;
    }

    public final String getLanguage() {
      return mLanguage;
    }

    public final float getScore() {
      return mScore;
    }
  }

  /** Returns the version of the LangId model used. */
  public int getVersion() {
    return nativeGetLangIdModelVersion(mModelPtr);
  }

  private static native long nativeNewLangIdModel(int fd);

  private static native long nativeNewLangIdModelFromPath(String path);

  private static native LanguageResult[] nativeDetectLanguages(long context, String text);

  private static native void nativeCloseLangIdModel(long context);

  private static native int nativeGetLangIdModelVersion(long context);
}
