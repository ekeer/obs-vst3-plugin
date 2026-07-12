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

#include "obs-vst3.h"

#include <cstdint>
#include <string>
#include <vector>

/* ---------- Module entry points (from plugin-main.cpp) ---------- */
bool load_host();
void unload_host();

/* ---------- VST3 list (scan, cache, free) ---------- */
bool retrieve_vst3_list();
void free_vst3_list();

/* ---------- OBS filter source registration ---------- */
void register_vst3_source();

/* ---------- Filter lifecycle ---------- */
void *vst3_create(obs_data_t *settings, obs_source_t *filter);
void vst3_destroy(void *data);

/* ---------- Filter update ---------- */
void vst3_update(void *data, obs_data_t *settings);
bool init_VST3Plugin(void *data, obs_data *settings);
bool create_VST3Plugin(struct vst3_audio_data *vd);
void destroy_current_VST3Plugin(struct vst3_audio_data *vd, obs_data *settings);

/* ---------- Filter save ---------- */
void vst3_save(void *data, obs_data_t *settings);

/* ---------- Utility functions ---------- */
std::string toHex(const std::vector<uint8_t> &data);
std::vector<uint8_t> fromHex(const std::string &hex);
uint64_t obs_to_vst3_speaker_arrangement(speaker_layout layout);
enum speaker_layout convert_speaker_layout(uint8_t channels);
