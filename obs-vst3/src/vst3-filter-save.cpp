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

#include "vst3-filter.h"
#include "VST3Plugin.h"

void vst3_save(void *data, obs_data_t *settings)
{
    vst3_audio_data *vd = (vst3_audio_data *)data;
    if (!vd) {
        return;
    }

    auto plugin = std::atomic_load(&vd->plugin);
    if (plugin) {
        std::vector<uint8_t> comp, ctrl;
        if (plugin->saveStates(comp, ctrl)) {
            obs_data_set_string(
                settings, "vst3_state",
                toHex(comp).c_str());
            if (!ctrl.empty()) {
                obs_data_set_string(
                    settings, "vst3_ctrl_state",
                    toHex(ctrl).c_str());
            } else {
                obs_data_set_string(
                    settings, "vst3_ctrl_state", "");
            }
        }
        // Store these because the filter might load before
        // VST3s list has been populated
        obs_data_set_string(
            settings, "vst3_path",
            vd->vst3_path.c_str());
        obs_data_set_string(
            settings, "vst3_name",
            vd->vst3_name.c_str());
    }
}
