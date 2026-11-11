/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.gts4lv.volumetile;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.drawable.Icon;
import android.media.AudioManager;
import android.service.quicksettings.Tile;
import android.service.quicksettings.TileService;

/**
 * Quick Settings tile showing the media volume. A tap opens the system volume dialog through
 * {@link SystemVolumeActivity}; a long-press opens Settings > Sound & vibration.
 */
public class VolumeTileService extends TileService {

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

    @Override
    public void onClick() {
        Intent intent = new Intent(this, SystemVolumeActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivityAndCollapse(PendingIntent.getActivity(
                this, 0, intent, PendingIntent.FLAG_IMMUTABLE));
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
