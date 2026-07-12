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

#pragma once

#include <obs-module.h>
#include <media-io/audio-resampler.h>
#include <util/deque.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#define MAX_PREPROC_CHANNELS 8
#define MAX_SC_CHANNELS 2
#define BUFFER_SIZE_MSEC 10
#define FRAME_SIZE 480

class VST3Plugin;
struct VST3Graph;

/* Audio packet info for dequeue */
struct vst3_audio_info {
	uint32_t frames;
	uint64_t timestamp;
};

struct sidechain_prop_info {
	obs_property_t *sources;
	obs_source_t *parent;
};

/* VST3 filter constants */
#define MT_ obs_module_text
#define S_PLUGIN "vst3_plugin"
#define S_EDITOR "vst3_open_gui"
#define S_SIDECHAIN_SOURCE "sidechain_source"
#define S_NOGUI "vst3_noview"
#define S_ERR "vst3_error"
#define S_SCAN "vst3_scan"

#define TEXT_EDITOR MT_("VST3.Button")
#define TEXT_PLUGIN MT_("VST3.Plugin")
#define TEXT_SIDECHAIN_SOURCE MT_("VST3.SidechainSource")
#define TEXT_NOGUI MT_("VST3.NOGUI")
#define TEXT_ERR MT_("VST3.Init.Fail")
#define TEXT_SCAN MT_("VST3.Scan.Ongoing")

struct vst3_audio_data {
	obs_source_t *context;

	std::shared_ptr<VST3Plugin> plugin = nullptr;
	std::string vst3_id;
	std::string vst3_path;
	std::string vst3_name;

	uint32_t sample_rate;
	size_t frames;
	size_t channels;
	speaker_layout layout;
	int64_t running_sample_count = 0;
	uint64_t system_time = 0;
	uint64_t last_timestamp;
	uint64_t latency;

	struct deque info_buffer;
	struct deque input_buffers[MAX_PREPROC_CHANNELS];
	struct deque output_buffers[MAX_PREPROC_CHANNELS];
	struct deque sc_input_buffers[MAX_PREPROC_CHANNELS];

	float *copy_buffers[MAX_PREPROC_CHANNELS];
	float *sc_copy_buffers[MAX_PREPROC_CHANNELS];

	struct obs_audio_data output_audio;
	DARRAY(float) output_data;

	std::atomic<bool> bypass;
	std::atomic<bool> sidechain_enabled;
	std::atomic<bool> noview;
	std::atomic_flag init_in_progress = ATOMIC_FLAG_INIT;

	std::atomic<bool> has_sidechain;
	obs_weak_source_t *weak_sidechain;
	std::string sidechain_name;
	uint64_t sidechain_check_time;
	std::shared_ptr<audio_resampler> sc_resampler;
	size_t sc_channels;
	uint64_t sc_last_timestamp;
	std::mutex sidechain_update_mutex;
	std::mutex sidechain_mutex;

	bool last_init_failed;
};
