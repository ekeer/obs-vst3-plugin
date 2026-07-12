/******************************************************************************
    Copyright (C) 2025-2026 pkv <pkv@obsproject.com>
    This file is part of obs-vst3.
    It uses the Steinberg VST3 SDK, which is licensed under MIT license.
    See https://github.com/steinbergmedia/vst3sdk for details.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "vst3-filter-sidechain.h"
#include "vst3-filter-audio.h"
#include "VST3Plugin.h"

#include <algorithm>

void teardown_sidechain(vst3_audio_data *vd, obs_data *settings)
{
    if (!vd) {
        return;
    }

    if (!vd->weak_sidechain || vd->sidechain_name.empty()) {
        return;
    }
    vd->sidechain_enabled.store(false, std::memory_order_relaxed);

    obs_weak_source_t *old_weak = nullptr;
    {
        std::lock_guard<std::mutex> lock(vd->sidechain_update_mutex);
        if (vd->weak_sidechain) {
            old_weak = vd->weak_sidechain;
            vd->weak_sidechain = nullptr;
        }
        vd->sidechain_name.clear();
        obs_data_set_string(settings, S_SIDECHAIN_SOURCE, nullptr);
    }

    if (old_weak) {
        obs_source_t *old_source =
            obs_weak_source_get_source(old_weak);
        if (old_source) {
            obs_source_remove_audio_capture_callback(
                old_source, sidechain_capture, vd);
            obs_source_release(old_source);
        }
        obs_weak_source_release(old_weak);
    }

    vd->sc_last_timestamp = 0;
    std::atomic_store(&vd->sc_resampler,
                      std::shared_ptr<audio_resampler>{});
}

void sidechain_swap(vst3_audio_data *vd, obs_data *settings)
{
    if (!vd) {
        return;
    }

    if (!vd->has_sidechain.load(std::memory_order_relaxed)) {
        return;
    }

    vd->sidechain_enabled.store(false, std::memory_order_relaxed);

    std::string sidechain_name(
        obs_data_get_string(settings, S_SIDECHAIN_SOURCE));
    bool valid_sidechain =
        sidechain_name != "none" && !sidechain_name.empty();
    obs_weak_source_t *old_weak_sidechain = nullptr;

    {
        std::lock_guard<std::mutex> lock(vd->sidechain_update_mutex);
        if (!valid_sidechain) {
            if (vd->weak_sidechain) {
                old_weak_sidechain = vd->weak_sidechain;
                vd->weak_sidechain = nullptr;
            }
            vd->sidechain_name = "";
        } else {
            if (vd->sidechain_name.empty() ||
                vd->sidechain_name != sidechain_name) {
                if (vd->weak_sidechain) {
                    old_weak_sidechain = vd->weak_sidechain;
                    vd->weak_sidechain = nullptr;
                }
                vd->sidechain_name = sidechain_name;
                vd->sidechain_check_time =
                    os_gettime_ns() - 3000000000;
            }
        }
    }
    vd->sidechain_enabled.store(true, std::memory_order_relaxed);

    if (old_weak_sidechain) {
        obs_source_t *old_sidechain =
            obs_weak_source_get_source(old_weak_sidechain);
        if (old_sidechain) {
            obs_source_remove_audio_capture_callback(
                old_sidechain, sidechain_capture, vd);
            obs_source_release(old_sidechain);
        }
        obs_weak_source_release(old_weak_sidechain);
    }
}

void sidechain_capture(void *data, obs_source_t *source,
                       const struct audio_data *audio, bool muted)
{
    UNUSED_PARAMETER(source);
    UNUSED_PARAMETER(muted);
    struct vst3_audio_data *vd =
        (struct vst3_audio_data *)data;
    auto p = std::atomic_load(&vd->plugin);
    bool bypass = vd->bypass.load(std::memory_order_relaxed);
    bool sc_enabled =
        vd->sidechain_enabled.load(std::memory_order_relaxed);

    if (bypass || !p || !sc_enabled) {
        return;
    }

    if (vd->sc_channels != 1 && vd->sc_channels != 2) {
        return;
    }

    if (vd->sc_last_timestamp) {
        int64_t diff = llabs(
            (int64_t)vd->sc_last_timestamp -
            (int64_t)audio->timestamp);
        if (diff > 1000000000LL) {
            reset_sidechain_data(vd);
        }
    }

    vd->sc_last_timestamp = audio->timestamp;

    {
        std::lock_guard<std::mutex> lock(vd->sidechain_mutex);
        for (size_t i = 0; i < vd->channels; i++) {
            deque_push_back(&vd->sc_input_buffers[i],
                            audio->data[i],
                            audio->frames * sizeof(float));
        }
    }
}

void vst3_tick(void *data, float seconds)
{
    struct vst3_audio_data *vd =
        (struct vst3_audio_data *)data;
    if (!vd) {
        return;
    }

    bool has_sc =
        vd->has_sidechain.load(std::memory_order_relaxed);
    if (!has_sc) {
        return;
    }

    std::string new_name = {};
    {
        std::lock_guard<std::mutex> lock(vd->sidechain_update_mutex);
        if (!vd->sidechain_name.empty() && !vd->weak_sidechain) {
            uint64_t t = os_gettime_ns();
            if (t - vd->sidechain_check_time > 3000000000) {
                new_name = vd->sidechain_name;
                vd->sidechain_check_time = t;
            }
        }
    }

    if (!new_name.empty()) {
        obs_source_t *sidechain =
            obs_get_source_by_name(new_name.c_str());
        obs_weak_source_t *weak_sidechain =
            sidechain ? obs_source_get_weak_source(sidechain)
                      : nullptr;
        {
            std::lock_guard<std::mutex> lock(
                vd->sidechain_update_mutex);
            if (!vd->sidechain_name.empty() &&
                vd->sidechain_name == new_name) {
                vd->weak_sidechain = weak_sidechain;
                weak_sidechain = nullptr;
            }
        }
        if (sidechain) {
            bool needs_resampling =
                vd->channels != vd->sc_channels;
            if (needs_resampling) {
                struct resample_info src, dst;
                src.samples_per_sec = vd->sample_rate;
                src.format = AUDIO_FORMAT_FLOAT_PLANAR;
                src.speakers =
                    convert_speaker_layout(
                        (uint8_t)vd->channels);
                dst.samples_per_sec = vd->sample_rate;
                dst.format = AUDIO_FORMAT_FLOAT_PLANAR;
                dst.speakers =
                    convert_speaker_layout(
                        (uint8_t)vd->sc_channels);
                audio_resampler *raw =
                    audio_resampler_create(&dst, &src);
                if (!raw) {
                    std::atomic_store(
                        &vd->sc_resampler,
                        std::shared_ptr<audio_resampler>{});
                } else {
                    std::shared_ptr<audio_resampler> sp(
                        raw, [](audio_resampler *r) {
                            if (r)
                                audio_resampler_destroy(r);
                        });
                    std::atomic_store(&vd->sc_resampler, sp);
                }
            } else {
                std::atomic_store(
                    &vd->sc_resampler,
                    std::shared_ptr<audio_resampler>{});
            }
            obs_source_add_audio_capture_callback(
                sidechain, sidechain_capture, vd);
            obs_weak_source_release(weak_sidechain);
            obs_source_release(sidechain);
        }
    }
    UNUSED_PARAMETER(seconds);
}
