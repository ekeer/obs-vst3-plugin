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

#include "vst3-filter-audio.h"
#include "VST3Plugin.h"

#include <algorithm>
#include <cstring>

/* -------------------------------------------------------- */
#define do_log(level, format, ...)                             \
    blog(level, "[VST3 filter ('%s')]: " format,              \
         obs_source_get_name(vd->context), ##__VA_ARGS__)

#define infovst3(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)
/* -------------------------------------------------------- */

static inline void clear_deque(struct deque *buf)
{
    deque_pop_front(buf, nullptr, buf->size);
}

void reset_data(struct vst3_audio_data *vd)
{
    for (size_t i = 0; i < vd->channels; i++) {
        clear_deque(&vd->input_buffers[i]);
        clear_deque(&vd->output_buffers[i]);
    }
    clear_deque(&vd->info_buffer);
}

void reset_sidechain_data(struct vst3_audio_data *vd)
{
    std::lock_guard<std::mutex> lock(vd->sidechain_mutex);
    for (size_t i = 0; i < vd->channels; i++) {
        clear_deque(&vd->sc_input_buffers[i]);
    }
}

static inline void preprocess_input(
    struct vst3_audio_data *vd,
    const std::shared_ptr<VST3Plugin> &plugin)
{
    int num_channels = (int)vd->channels;
    int sc_num_channels = (int)vd->sc_channels;
    int frames = (int)vd->frames;
    size_t segment_size = vd->frames * sizeof(float);
    bool has_sc = vd->has_sidechain.load(std::memory_order_relaxed);
    bool sc_enabled =
        vd->sidechain_enabled.load(std::memory_order_relaxed);

    if (has_sc && sc_enabled) {
        std::lock_guard<std::mutex> lock(vd->sidechain_mutex);
        for (int i = 0; i < num_channels; i++) {
            if (vd->sc_input_buffers[i].size < segment_size) {
                deque_push_back_zero(&vd->sc_input_buffers[i],
                                     segment_size);
            }
        }
    }

    // Pop from input deque (main + sc)
    for (int i = 0; i < num_channels; i++) {
        deque_pop_front(&vd->input_buffers[i],
                        vd->copy_buffers[i],
                        vd->frames * sizeof(float));
    }

    if (has_sc && sc_enabled) {
        std::lock_guard<std::mutex> lock(vd->sidechain_mutex);
        for (int i = 0; i < num_channels; i++) {
            deque_pop_front(&vd->sc_input_buffers[i],
                            vd->sc_copy_buffers[i],
                            vd->frames * sizeof(float));
        }
    }

    // Copy input OBS buffer to VST input buffers
    for (int ch = 0; ch < num_channels; ++ch) {
        float *inBuf = (float *)vd->copy_buffers[ch];
        float *vstIn = plugin->channelBuffer32(
            Steinberg::Vst::kInput, ch);
        if (inBuf && vstIn) {
            memcpy(vstIn, inBuf, frames * sizeof(float));
        }
    }

    // Copy sidechain input to VST aux input buffers
    if (has_sc && sc_enabled) {
        bool needs_resampling =
            vd->channels != vd->sc_channels &&
            (vd->sc_channels == 1 || vd->sc_channels == 2);
        auto sc_resampler =
            std::atomic_load(&vd->sc_resampler);
        if (needs_resampling && sc_resampler) {
            uint8_t *resampled[2] = {nullptr, nullptr};
            uint32_t out_frames;
            uint64_t ts_offset;

            if (audio_resampler_resample(
                    sc_resampler.get(), resampled, &out_frames,
                    &ts_offset,
                    (const uint8_t **)vd->sc_copy_buffers,
                    (uint32_t)vd->frames)) {
                for (int ch = 0; ch < sc_num_channels; ++ch) {
                    float *inBuf = (float *)resampled[ch];
                    float *vstIn = plugin->auxChannelBuffer32(
                        Steinberg::Vst::kInput, ch);
                    if (inBuf && vstIn) {
                        memcpy(vstIn, inBuf,
                               out_frames * sizeof(float));
                    }
                }
            }
        } else {
            for (int ch = 0; ch < sc_num_channels; ++ch) {
                float *inBuf = vd->sc_copy_buffers[ch];
                float *vstIn = plugin->auxChannelBuffer32(
                    Steinberg::Vst::kInput, ch);
                if (inBuf && vstIn) {
                    memcpy(vstIn, inBuf,
                           frames * sizeof(float));
                }
            }
        }
    }
}

void process_audio(struct vst3_audio_data *vd,
                   const std::shared_ptr<VST3Plugin> &plugin)
{
    int num_channels = (int)vd->channels;
    int frames = (int)vd->frames;

    preprocess_input(vd, plugin);
    plugin->process(frames);

    // Retrieve processed buffers from VST
    for (int ch = 0; ch < num_channels; ++ch) {
        uint8_t *outBuf = (uint8_t *)vd->copy_buffers[ch];
        float *vstOut = plugin->channelBuffer32(
            Steinberg::Vst::kOutput, ch);
        if (outBuf && vstOut) {
            memcpy(outBuf, vstOut, frames * sizeof(float));
        }
    }

    // Push to output deque
    for (size_t i = 0; i < vd->channels; i++) {
        deque_push_back(&vd->output_buffers[i],
                        vd->copy_buffers[i],
                        vd->frames * sizeof(float));
    }
}

struct obs_audio_data *vst3_filter_audio(void *data,
                                          struct obs_audio_data *audio)
{
    vst3_audio_data *vd = (vst3_audio_data *)data;
    struct vst3_audio_info info;
    size_t segment_size = vd->frames * sizeof(float);
    size_t out_size;
    auto p = std::atomic_load(&vd->plugin);
    bool bypass = vd->bypass.load(std::memory_order_relaxed);

    if (bypass || !p) {
        return audio;
    }

    if (!p->numEnabledOutputAudioBuses) {
        return audio;
    }

    /* If timestamp has dramatically changed, clear deques */
    if (vd->last_timestamp) {
        int64_t diff = llabs(
            (int64_t)vd->last_timestamp -
            (int64_t)audio->timestamp);

        if (diff > 1000000000LL) {
            reset_data(vd);
        }
    }

    vd->last_timestamp = audio->timestamp;

    /* push audio packet info to info deque */
    info.frames = audio->frames;
    info.timestamp = audio->timestamp;
    deque_push_back(&vd->info_buffer, &info, sizeof(info));

    /* push back current audio data to input deque */
    for (size_t i = 0; i < vd->channels; i++) {
        deque_push_back(&vd->input_buffers[i],
                        audio->data[i],
                        audio->frames * sizeof(float));
    }

    /* pop/process each 10ms segment */
    while (vd->input_buffers[0].size >= segment_size) {
        process_audio(vd, p);
    }

    /* peek front of info deque */
    memset(&info, 0, sizeof(info));
    deque_peek_front(&vd->info_buffer, &info, sizeof(info));
    out_size = info.frames * sizeof(float);

    if (vd->output_buffers[0].size < out_size) {
        return nullptr;
    }

    /* pop and return a packet */
    deque_pop_front(&vd->info_buffer, nullptr, sizeof(info));
    da_resize(vd->output_data, out_size * vd->channels);

    for (size_t i = 0; i < vd->channels; i++) {
        vd->output_audio.data[i] =
            (uint8_t *)&vd->output_data.array[i * out_size];

        deque_pop_front(&vd->output_buffers[i],
                        vd->output_audio.data[i], out_size);
    }

    vd->running_sample_count += info.frames;
    vd->system_time = os_gettime_ns();
    vd->output_audio.frames = info.frames;
    vd->output_audio.timestamp =
        info.timestamp - vd->latency;
    return &vd->output_audio;
}
