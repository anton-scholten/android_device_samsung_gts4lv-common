/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "audio_hw_gts4lv"

#include <dlfcn.h>
#include <string.h>

#include <string>

#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

namespace {

constexpr char kStockLibPath[] = "/vendor/lib/hw/audio.primary.sdm710.so";

constexpr int kSlotPositions0[] = {0, 1, 0, 1};
constexpr int kSlotPositions90[] = {1, 1, 0, 0};
constexpr int kSlotPositions180[] = {1, 0, 1, 0};
constexpr int kSlotPositions270[] = {0, 0, 1, 1};

hw_module_t* gStockModule;
int (*gStockSetParameters)(audio_hw_device*, const char*);
int (*gStockSetMode)(audio_hw_device*, audio_mode_t);

void setMixerValueByName(mixer* mixer, const char* name, int value) {
    const auto ctl = mixer_get_ctl_by_name(mixer, name);

    if (ctl == nullptr) {
        ALOGE("Failed to find mixer ctl for %s", name);
        return;
    }

    if (mixer_ctl_set_value(ctl, 0, value) < 0) {
        ALOGE("Failed to set ctl value %d for %s", value, name);
    }
}

void setSlotPositions(const int* values) {
    const auto mixer = mixer_open(0);

    if (mixer == nullptr) {
        ALOGE("Failed to open mixer");
        return;
    }

    setMixerValueByName(mixer, "FL ASPRX1 Slot Position", values[0]);
    setMixerValueByName(mixer, "FR ASPRX1 Slot Position", values[1]);
    setMixerValueByName(mixer, "RL ASPRX1 Slot Position", values[2]);
    setMixerValueByName(mixer, "RR ASPRX1 Slot Position", values[3]);

    mixer_close(mixer);
}

void setAfeProxyMixers(bool enabled) {
    const auto mixer = mixer_open(0);

    if (mixer == nullptr) {
        ALOGE("Failed to open mixer");
        return;
    }

    for (int i = 1; i <= 16; i++) {
        const auto name = "AFE_PCM_RX Audio Mixer MultiMedia" + std::to_string(i);
        setMixerValueByName(mixer, name.c_str(), enabled);
    }

    mixer_close(mixer);
}

int adevSetParameters(audio_hw_device* dev, const char* kvpairs) {
    const auto parms = str_parms_create_str(kvpairs);

    if (parms == nullptr) {
        return gStockSetParameters(dev, kvpairs);
    }

    std::string params = kvpairs;
    char value[32];
    int intValue;

    if (str_parms_get_str(parms, "rotation", value, sizeof(value)) >= 0) {
        if (!strcmp(value, "0")) {
            setSlotPositions(kSlotPositions0);
        } else if (!strcmp(value, "90")) {
            setSlotPositions(kSlotPositions90);
        } else if (!strcmp(value, "180")) {
            setSlotPositions(kSlotPositions180);
        } else if (!strcmp(value, "270")) {
            setSlotPositions(kSlotPositions270);
        }

        // Rotation is handled entirely here, don't forward it to the stock HAL.
        str_parms_del(parms, "rotation");
        const auto str = str_parms_to_str(parms);
        params = str;
        free(str);
    }

    if (str_parms_get_str(parms, "bt_wbs", value, sizeof(value)) >= 0) {
        params += ";g_sco_samplerate=";
        params += !strcmp(value, "on") ? "16000" : "8000";
    }

    if (str_parms_get_int(parms, "connect", &intValue) >= 0 && intValue == AUDIO_DEVICE_OUT_PROXY) {
        setAfeProxyMixers(true);
    } else if (str_parms_get_int(parms, "disconnect", &intValue) >= 0 &&
               intValue == AUDIO_DEVICE_OUT_PROXY) {
        setAfeProxyMixers(false);
    }

    str_parms_destroy(parms);

    return gStockSetParameters(dev, params.c_str());
}

int adevSetMode(audio_hw_device* dev, audio_mode_t mode) {
    // On regular QCOM devices, these parameters are set by QtiTelephonyService,
    // which basically listens to vendor.qti.hardware.radio.am callbacks.
    // The hardcoded vsid value here is decimal VOICEMMODE1_VSID which one can find
    // in {QC_AUDIO_HAL}/hal/voice_extn/voice_extn.c. The current code doesn't
    // handle VOICEMMODE2_VSID, but since we don't have any MSIM variants it's totally fine.
    // Audio param call_state 2 corresponds to CALL_ACTIVE and 1 to CALL_INACTIVE respectively,
    // both of which can be found in {QC_AUDIO_HAL}/hal/voice.h.
    gStockSetParameters(dev, mode == AUDIO_MODE_IN_CALL ? "call_state=2;vsid=297816064"
                                                        : "call_state=1;vsid=297816064");

    return gStockSetMode(dev, mode);
}

int adevOpen(const hw_module_t* /* module */, const char* name, hw_device_t** device) {
    if (gStockModule == nullptr) {
        const auto handle = dlopen(kStockLibPath, RTLD_NOW);

        if (handle == nullptr) {
            ALOGE("Failed to dlopen %s: %s", kStockLibPath, dlerror());
            return -EINVAL;
        }

        gStockModule = static_cast<hw_module_t*>(dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));

        if (gStockModule == nullptr) {
            ALOGE("Failed to find %s in %s", HAL_MODULE_INFO_SYM_AS_STR, kStockLibPath);
            dlclose(handle);
            return -EINVAL;
        }

        gStockModule->dso = handle;
    }

    const auto ret = gStockModule->methods->open(gStockModule, name, device);

    if (ret != 0) {
        return ret;
    }

    // The stock HAL casts the device back to its private struct in every callback,
    // so hook the function pointers in place rather than wrapping the device.
    const auto adev = reinterpret_cast<audio_hw_device*>(*device);

    gStockSetParameters = adev->set_parameters;
    gStockSetMode = adev->set_mode;
    adev->set_parameters = adevSetParameters;
    adev->set_mode = adevSetMode;

    return 0;
}

hw_module_methods_t gModuleMethods = {
        .open = adevOpen,
};

}  // namespace

audio_module HAL_MODULE_INFO_SYM = {
        .common =
                {
                        .tag = HARDWARE_MODULE_TAG,
                        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
                        .hal_api_version = HARDWARE_HAL_API_VERSION,
                        .id = AUDIO_HARDWARE_MODULE_ID,
                        .name = "gts4lv audio HAL wrapper",
                        .author = "The LineageOS Project",
                        .methods = &gModuleMethods,
                },
};
