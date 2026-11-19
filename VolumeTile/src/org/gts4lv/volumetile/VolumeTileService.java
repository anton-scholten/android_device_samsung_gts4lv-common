/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.gts4lv.volumetile;

import android.app.StatusBarManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.drawable.Icon;
import android.media.AudioManager;
import android.os.Handler;
import android.os.Looper;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/**
 * Quick Settings tile showing the media volume. A tap collapses the shade and opens the system
 * volume dialog over the current app; a long-press opens Settings > Sound & vibration.
 */
public class VolumeTileService extends TileService {

    // The volume dialog does not show over an expanded shade: wait for the collapse animation.
    private static final long SHOW_DELAY_MS = 350;

    private final Handler mHandler = new Handler(Looper.getMainLooper());

    private AudioManager mAudioManager;
    private boolean mReceiverRegistered;

    private final BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            updateTile();
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        mAudioManager = getSystemService(AudioManager.class);
    }

    @Override
    public void onStartListening() {
        if (!mReceiverRegistered) {
            IntentFilter filter = new IntentFilter(Volume.VOLUME_CHANGED_ACTION);
            filter.addAction(Volume.STREAM_MUTE_CHANGED_ACTION);
            registerReceiver(mReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
            mReceiverRegistered = true;
        }
        updateTile();
    }

    @Override
    public void onStopListening() {
        if (mReceiverRegistered) {
            unregisterReceiver(mReceiver);
            mReceiverRegistered = false;
        }
    }

    // No activity is started: that would count as leaving the current app, and video apps such
    // as YouTube then minimize or enter picture-in-picture.
    @Override
    public void onClick() {
        getSystemService(StatusBarManager.class).collapsePanels();
        mHandler.postDelayed(() -> mAudioManager.adjustStreamVolume(Volume.STREAM,
                AudioManager.ADJUST_SAME, AudioManager.FLAG_SHOW_UI), SHOW_DELAY_MS);
    }

    private void updateTile() {
        Tile tile = getQsTile();
        if (tile == null) {
            return;
        }
        int volume = Volume.current(mAudioManager);
        int max = mAudioManager.getStreamMaxVolume(Volume.STREAM);
        tile.setState(volume > 0 ? Tile.STATE_ACTIVE : Tile.STATE_INACTIVE);
        tile.setIcon(Icon.createWithResource(this,
                volume > 0 ? R.drawable.ic_volume : R.drawable.ic_volume_off));
        tile.setSubtitle(Math.round(100f * volume / max) + "%");
        tile.updateTile();
    }
}
