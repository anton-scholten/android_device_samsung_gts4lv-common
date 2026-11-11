/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.gts4lv.volumetile;

import android.media.AudioManager;

final class Volume {
    // AudioManager.VOLUME_CHANGED_ACTION and STREAM_MUTE_CHANGED_ACTION are hidden; the strings
    // are stable.
    static final String VOLUME_CHANGED_ACTION = "android.media.VOLUME_CHANGED_ACTION";
    static final String STREAM_MUTE_CHANGED_ACTION = "android.media.STREAM_MUTE_CHANGED_ACTION";

    static final int STREAM = AudioManager.STREAM_MUSIC;

    private Volume() {}

    static int current(AudioManager am) {
        return am.isStreamMute(STREAM) ? 0 : am.getStreamVolume(STREAM);
    }
}
