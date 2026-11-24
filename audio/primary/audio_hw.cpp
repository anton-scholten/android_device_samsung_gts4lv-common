/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "audio_hw_gts4lv"

#include <dlfcn.h>
#include <string.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

namespace {

constexpr char kStockLibPath[] = "/vendor/lib/hw/audio.primary.sdm710.so";
constexpr char kSoundBoosterLibPath[] = "lib_SoundBooster_ver_Quad_1000.so";
constexpr char kSoundBoosterParamPath[] = "/vendor/etc/SoundBoosterParam.txt";

constexpr int kSlotPositions0[] = {0, 1, 0, 1};
constexpr int kSlotPositions90[] = {1, 1, 0, 0};
constexpr int kSlotPositions180[] = {1, 0, 1, 0};
constexpr int kSlotPositions270[] = {0, 0, 1, 1};
// One TDM slot per amp, used when SoundBooster renders all four speaker channels.
constexpr int kSlotPositionsTdm[] = {0, 1, 2, 3};

// Offset of the device list in the stock HAL's stream_out struct, as passed to
// get_device_types() by start_output_stream().
constexpr size_t kStreamOutDevicesOffset = 224;

// SoundBooster mode used by the stock SoundBoosterTDM effect.
constexpr int kSoundBoosterMode = 2;
constexpr int kSoundBoosterBitDepth16 = 0;
constexpr int kSoundBoosterBitDepth824 = 1;
constexpr int kOutputChannelTypeTdm = 4;

// Volume table the stock SoundBoosterTDM effect passes along with the tuning file.
float gSoundBoosterVolumeTable[] = {0,  43, 49, 55, 60, 65, 70, 75, 77,
                                    80, 82, 85, 87, 91, 95, 100, 1};

hw_module_t* gStockModule;
int (*gStockSetParameters)(audio_hw_device*, const char*);
int (*gStockSetMode)(audio_hw_device*, audio_mode_t);
int (*gStockOpenOutputStream)(audio_hw_device*, audio_io_handle_t, audio_devices_t,
                              audio_output_flags_t, audio_config*, audio_stream_out**,
                              const char*);
void (*gStockCloseOutputStream)(audio_hw_device*, audio_stream_out*);
ssize_t (*gStockOutWrite)(audio_stream_out*, const void*, size_t);
int (*gGetOutputChannelType)(audio_stream_out*, uint32_t);
uint32_t (*gGetDeviceTypes)(void*);

std::atomic<audio_mode_t> gMode = AUDIO_MODE_NORMAL;
// SoundBooster orientation, 0-3; bumped generation makes streams re-apply it.
std::atomic<int> gOrientation = 0;
std::atomic<int> gOrientationGeneration = 0;

struct SoundBoosterLib {
    void* (*create)(int mode);
    void (*destroy)(void* sb);
    int (*init)(void* sb, int bitDepth);
    int (*samplingRateConfig)(void* sb, int sampleRate);
    int (*loadParameter)(void* sb, const char* path, bool engMode, float* volumeTable);
    int (*setOrientation)(void* sb, int orientation);
    int (*onOff)(void* sb, int enable);
    int (*buffClear)(void* sb);
    int (*exe)(void* sb, void* out, const void* in, int frames, float volume);
} gSb;
bool gSbLoaded;

struct StreamContext {
    void* sb = nullptr;
    int orientationGeneration = -1;
    int enabled = -1;
    std::vector<uint8_t> buffer;
};

std::mutex gStreamsLock;
std::map<audio_stream_out*, StreamContext> gStreams;

bool loadSoundBooster() {
    const auto handle = dlopen(kSoundBoosterLibPath, RTLD_NOW);

    if (handle == nullptr) {
        ALOGW("Failed to dlopen %s: %s", kSoundBoosterLibPath, dlerror());
        return false;
    }

    const auto sym = [handle](const char* name) {
        const auto ptr = dlsym(handle, name);

        if (ptr == nullptr) {
            ALOGE("Failed to find %s in %s", name, kSoundBoosterLibPath);
        }

        return ptr;
    };

#define LOAD(field, name) gSb.field = reinterpret_cast<decltype(gSb.field)>(sym(name))
    LOAD(create, "_ZN30SoundBooster_Interface_Factory6CreateEi");
    LOAD(destroy, "_ZN30SoundBooster_Interface_Factory7DestroyEP25SoundBooster_Interface_IF");
    LOAD(init, "_ZN22SoundBooster_Interface4InitE13SB_BitDepth_T");
    LOAD(samplingRateConfig, "_ZN22SoundBooster_Interface18SamplingRateConfigEi");
    LOAD(loadParameter, "_ZN22SoundBooster_Interface13LoadParameterEPKcbPf");
    LOAD(setOrientation,
         "_ZN22SoundBooster_Interface14SetOrientationE23SB_Device_Orientation_T");
    LOAD(onOff, "_ZN22SoundBooster_Interface17SoundBoosterOnOffEi");
    LOAD(buffClear, "_ZN22SoundBooster_Interface9BuffClearEv");
    LOAD(exe, "_ZN22SoundBooster_Interface3ExeEPvPKvif");
#undef LOAD

    if (!gSb.create || !gSb.destroy || !gSb.init || !gSb.samplingRateConfig ||
        !gSb.loadParameter || !gSb.setOrientation || !gSb.onOff || !gSb.buffClear || !gSb.exe) {
        dlclose(handle);
        return false;
    }

    return true;
}

void* createSoundBooster(uint32_t sampleRate, int bitDepth) {
    const auto sb = gSb.create(kSoundBoosterMode);

    if (sb == nullptr) {
        return nullptr;
    }

    gSb.init(sb, bitDepth);
    gSb.samplingRateConfig(sb, sampleRate);
    gSb.loadParameter(sb, kSoundBoosterParamPath, false, gSoundBoosterVolumeTable);

    return sb;
}

// Converts a display rotation in degrees to the orientation SoundBooster expects,
// mirroring the stock SoundBoosterTDM effect which swaps 90 and 270.
int rotationToOrientation(int degrees) {
    switch (degrees) {
        case 90:
            return 3;
        case 180:
            return 2;
        case 270:
            return 1;
        default:
            return 0;
    }
}

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
        if (gSbLoaded) {
            gOrientation = rotationToOrientation(atoi(value));
            gOrientationGeneration++;

            // Rotation for offloaded playback, which runs SoundBooster on the ADSP.
            // Takes a Surface rotation and swaps 90 and 270 itself.
            const auto rotation = "g_hw_display_rotation=" + std::to_string(atoi(value) / 90);
            gStockSetParameters(dev, rotation.c_str());
        } else if (!strcmp(value, "0")) {
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

    gMode = mode;

    return gStockSetMode(dev, mode);
}

// Fallback for writes SoundBooster can't process: duplicate L/R onto both speaker pairs.
void upmixToQuad(const uint8_t* in, uint8_t* out, size_t frames, size_t sampleSize) {
    for (size_t i = 0; i < frames; i++) {
        memcpy(out, in, sampleSize * 2);
        memcpy(out + sampleSize * 2, in, sampleSize * 2);
        in += sampleSize * 2;
        out += sampleSize * 4;
    }
}

ssize_t outWrite(audio_stream_out* stream, const void* buffer, size_t bytes) {
    const auto device =
            gGetDeviceTypes(reinterpret_cast<uint8_t*>(stream) + kStreamOutDevicesOffset);

    // The stock HAL expects four interleaved channels (twice the bytes of the stereo stream)
    // for speaker outputs once the TDM framework is enabled. Use the same check as its
    // start_output_stream() so the data always matches how the PCM was opened.
    if (gGetOutputChannelType(stream, device) != kOutputChannelTypeTdm) {
        return gStockOutWrite(stream, buffer, bytes);
    }

    const auto format = stream->common.get_format(&stream->common);
    const auto channels =
            audio_channel_count_from_out_mask(stream->common.get_channels(&stream->common));
    const auto sampleSize = audio_bytes_per_sample(format);

    if (channels != 2 || sampleSize == 0) {
        return gStockOutWrite(stream, buffer, bytes);
    }

    const size_t frames = bytes / (sampleSize * 2);

    // Only the lookup needs the lock: holding it over the blocking write below
    // starves other outputs (low latency underruns while deep buffer writes).
    StreamContext* ctxPtr;
    {
        std::lock_guard<std::mutex> lock(gStreamsLock);
        ctxPtr = &gStreams[stream];
    }
    auto& ctx = *ctxPtr;
    ctx.buffer.resize(frames * sampleSize * 4);

    // Deep buffer runs in 8.24 like on stock, so the HAL writes it to the S24 TDM PCM as is.
    if (ctx.sb == nullptr &&
        (format == AUDIO_FORMAT_PCM_16_BIT || format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        ctx.sb = createSoundBooster(stream->common.get_sample_rate(&stream->common),
                                    format == AUDIO_FORMAT_PCM_8_24_BIT ? kSoundBoosterBitDepth824
                                                                        : kSoundBoosterBitDepth16);
    }

    if (ctx.sb != nullptr) {
        const int generation = gOrientationGeneration;

        if (ctx.orientationGeneration != generation) {
            gSb.setOrientation(ctx.sb, gOrientation);
            ctx.orientationGeneration = generation;
        }

        // Like the stock effect, only tune for speaker-only playback outside of VoIP,
        // otherwise just route the stereo signal to all four speakers.
        const int enabled = device == AUDIO_DEVICE_OUT_SPEAKER &&
                            gMode != AUDIO_MODE_IN_COMMUNICATION;

        if (ctx.enabled != enabled) {
            gSb.onOff(ctx.sb, enabled);
            gSb.buffClear(ctx.sb);
            ctx.enabled = enabled;
        }

        gSb.exe(ctx.sb, ctx.buffer.data(), buffer, frames, 0.0f);
    } else {
        upmixToQuad(static_cast<const uint8_t*>(buffer), ctx.buffer.data(), frames, sampleSize);
    }

    const auto ret = gStockOutWrite(stream, ctx.buffer.data(), ctx.buffer.size());

    return ret > 0 ? ret / 2 : ret;
}

int adevOpenOutputStream(audio_hw_device* dev, audio_io_handle_t handle, audio_devices_t devices,
                         audio_output_flags_t flags, audio_config* config,
                         audio_stream_out** streamOut, const char* address) {
    const auto ret =
            gStockOpenOutputStream(dev, handle, devices, flags, config, streamOut, address);

    if (ret == 0 && gSbLoaded) {
        gStockOutWrite = (*streamOut)->write;
        (*streamOut)->write = outWrite;
    }

    return ret;
}

void adevCloseOutputStream(audio_hw_device* dev, audio_stream_out* stream) {
    {
        std::lock_guard<std::mutex> lock(gStreamsLock);
        const auto it = gStreams.find(stream);

        if (it != gStreams.end()) {
            if (it->second.sb != nullptr) {
                gSb.destroy(it->second.sb);
            }

            gStreams.erase(it);
        }
    }

    gStockCloseOutputStream(dev, stream);
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
    gStockOpenOutputStream = adev->open_output_stream;
    gStockCloseOutputStream = adev->close_output_stream;
    adev->set_parameters = adevSetParameters;
    adev->set_mode = adevSetMode;
    adev->open_output_stream = adevOpenOutputStream;
    adev->close_output_stream = adevCloseOutputStream;

    // Feed the four speaker amps separately through SoundBooster (stock SoundBoosterTDM),
    // falling back to duplicating stereo onto both amp pairs if it's unavailable.
    gGetOutputChannelType = reinterpret_cast<decltype(gGetOutputChannelType)>(
            dlsym(gStockModule->dso, "sec_get_output_channel_type"));
    gGetDeviceTypes = reinterpret_cast<decltype(gGetDeviceTypes)>(
            dlsym(gStockModule->dso, "get_device_types"));
    gSbLoaded = gGetOutputChannelType != nullptr && gGetDeviceTypes != nullptr &&
                loadSoundBooster();

    if (gSbLoaded) {
        setSlotPositions(kSlotPositionsTdm);
        gStockSetParameters(adev, "l_effect_tdm_framework_enable=true");
    }

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
