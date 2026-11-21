/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package org.gts4lv.corners;

import android.content.Context;
import android.content.om.OverlayInfo;
import android.content.om.OverlayManager;
import android.os.Bundle;
import android.util.Log;

import com.android.settingslib.drawer.EntryController;
import com.android.settingslib.drawer.ProviderSwitch;
import com.android.settingslib.drawer.TileUtils;

/** The switch is on (rounded corners) while the square-corners overlay is disabled. */
public class CornersSwitchController extends EntryController implements ProviderSwitch {

    private static final String TAG = "CornersSwitch";
    private static final String OVERLAY = "org.gts4lv.overlay.squarecorners";
    private static final String CATEGORY_DISPLAY = "com.android.settings.category.ia.display";

    private final Context mContext;
    private final OverlayManager mOverlayManager;

    public CornersSwitchController(Context context) {
        mContext = context;
        mOverlayManager = context.getSystemService(OverlayManager.class);
    }

    @Override
    public String getKey() {
        return "rounded_corners";
    }

    @Override
    protected MetaData getMetaData() {
        return new GroupMetaData(CATEGORY_DISPLAY)
                .setTitle(R.string.corners_title)
                .setSummary(R.string.corners_summary)
                .setOrder(-1);
    }

    @Override
    public boolean isSwitchChecked() {
        OverlayInfo info = mOverlayManager.getOverlayInfo(OVERLAY, mContext.getUser());
        return info == null || !info.isEnabled();
    }

    @Override
    public boolean onSwitchCheckedChanged(boolean checked) {
        try {
            mOverlayManager.setEnabled(OVERLAY, !checked, mContext.getUser());
            return true;
        } catch (RuntimeException e) {
            Log.e(TAG, "Failed to set " + OVERLAY, e);
            return false;
        }
    }

    @Override
    public String getSwitchErrorMessage(boolean attemptedChecked) {
        return mContext.getString(R.string.corners_error);
    }

    /** MetaData does not set a group; place the switch under "Other display controls". */
    private static class GroupMetaData extends MetaData {
        GroupMetaData(String category) {
            super(category);
        }

        @Override
        protected Bundle build() {
            Bundle bundle = super.build();
            bundle.putString(TileUtils.META_DATA_PREFERENCE_GROUP_KEY, "category_other");
            return bundle;
        }
    }
}
