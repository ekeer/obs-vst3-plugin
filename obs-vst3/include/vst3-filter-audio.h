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

/* ---------- Audio processing entry point ---------- */
struct obs_audio_data *vst3_filter_audio(void *data, struct obs_audio_data *audio);

/* ---------- Internal processing ---------- */
void process_audio(struct vst3_audio_data *vd,
                   const std::shared_ptr<VST3Plugin> &plugin);

/* ---------- Data reset helpers ---------- */
void reset_data(struct vst3_audio_data *vd);
void reset_sidechain_data(struct vst3_audio_data *vd);
