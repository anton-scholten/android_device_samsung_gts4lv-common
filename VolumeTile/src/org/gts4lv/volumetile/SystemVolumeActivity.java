/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.gts4lv.volumetile;

import android.app.Activity;
import android.media.AudioManager;
import android.os.Bundle;

/**
 * Shows the system volume dialog (the one the volume keys open) for media volume, without
 * changing the volume. Started by the tile once the shade has collapsed: the dialog does not
 * show over an expanded shade.
 */
public class SystemVolumeActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getSystemService(AudioManager.class).adjustStreamVolume(Volume.STREAM,
                AudioManager.ADJUST_SAME, AudioManager.FLAG_SHOW_UI);
        finish();
    }
}
