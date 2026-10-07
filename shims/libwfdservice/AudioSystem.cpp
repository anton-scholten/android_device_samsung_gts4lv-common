/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <media/AudioSystem.h>

using android::AudioSystem;
using android::status_t;
using android::media::audio::common::AudioPort;

// libwfdservice was built against the Android 15 QPR2 signature, before
// Android 16 added the deviceSwitch argument. Provide the old symbol and
// forward to the new one.
extern "C" status_t
_ZN7android11AudioSystem24setDeviceConnectionStateE24audio_policy_dev_state_tRKNS_5media5audio6common9AudioPortE14audio_format_t(
        audio_policy_dev_state_t state, const AudioPort& port, audio_format_t encodedFormat) {
    return AudioSystem::setDeviceConnectionState(state, port, encodedFormat, false);
}
