/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.gts4lv.corners;

import android.os.Binder;
import android.os.Bundle;
import android.os.Process;

import com.android.settingslib.drawer.EntriesProvider;
import com.android.settingslib.drawer.EntryController;

import java.util.List;

public class CornersSwitchProvider extends EntriesProvider {

    @Override
    protected List<? extends EntryController> createEntryControllers() {
        return List.of(new CornersSwitchController(getContext()));
    }

    @Override
    public Bundle call(String method, String uri, Bundle extras) {
        // Only Settings (system uid) may read or flip the switch.
        if (Binder.getCallingUid() != Process.SYSTEM_UID) {
            throw new SecurityException("Caller not allowed");
        }
        return super.call(method, uri, extras);
    }
}
