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

#include "vst3-filter-properties.h"
#include "VST3Plugin.h"
#include "VST3Scanner.h"

#include <obs-module.h>

extern VST3Scanner *g_scanner_list_;
extern std::atomic<bool> g_vst3_scan_done_;

bool vst3_show_gui_callback(obs_properties_t *props,
                            obs_property_t *p, void *data)
{
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(p);
    vst3_audio_data *vd = static_cast<vst3_audio_data *>(data);
    if (!vd) {
        return false;
    }

    auto plugin = std::atomic_load(&vd->plugin);
    if (!plugin) {
        return false;
    }

    bool noview = vd->noview.load(std::memory_order_relaxed);
    if (noview) {
        return false;
    }

    if (!plugin->isEditorVisible()) {
        plugin->showEditor();
    } else {
        plugin->hideEditor();
    }

    return true;
}

static bool add_sources(void *data, obs_source_t *source)
{
    struct sidechain_prop_info *info =
        (struct sidechain_prop_info *)data;
    uint32_t caps = obs_source_get_output_flags(source);

    if (source == info->parent) {
        return true;
    }

    if ((caps & OBS_SOURCE_AUDIO) == 0) {
        return true;
    }

    const char *name = obs_source_get_name(source);
    obs_property_list_add_string(info->sources, name, name);
    return true;
}

bool on_vst3_changed_cb(void *priv, obs_properties_t *props,
                        obs_property_t *property, obs_data_t *settings)
{
    UNUSED_PARAMETER(property);
    UNUSED_PARAMETER(settings);
    auto vd = (struct vst3_audio_data *)priv;
    if (!vd) {
        return false;
    }

    bool has_sc =
        vd->has_sidechain.load(std::memory_order_relaxed);

    obs_property_t *gui = obs_properties_get(props, S_EDITOR);
    obs_property_set_visible(
        gui, !vd->noview.load(std::memory_order_relaxed) &&
             !vd->last_init_failed);

    obs_property_t *p =
        obs_properties_get(props, S_SIDECHAIN_SOURCE);
    if (has_sc && !vd->last_init_failed) {
        obs_source_t *parent =
            obs_filter_get_parent(vd->context);
        obs_property_list_clear(p);
        obs_property_list_add_string(
            p, obs_module_text("None"), "none");
        struct sidechain_prop_info info = {p, parent};
        obs_enum_sources(add_sources, &info);
        obs_property_set_visible(p, true);
    } else {
        obs_property_set_visible(p, false);
    }

    obs_property_t *noview =
        obs_properties_get(props, S_NOGUI);
    obs_property_set_visible(
        noview,
        vd->noview.load(std::memory_order_relaxed) &&
            !vd->last_init_failed);

    obs_property_t *err = obs_properties_get(props, S_ERR);
    if (err) {
        obs_properties_remove_by_name(props, S_ERR);
    }
    if (vd->last_init_failed) {
        obs_property_t *err2 = obs_properties_add_text(
            props, S_ERR, TEXT_ERR, OBS_TEXT_INFO);
        obs_property_text_set_info_type(err2, OBS_TEXT_INFO_ERROR);
    }
    return true;
}

obs_properties_t *vst3_properties(void *data)
{
    auto vd = (struct vst3_audio_data *)data;
    obs_properties_t *props = obs_properties_create();
    obs_property_t *sources;

    obs_property_t *vst3list = obs_properties_add_list(
        props, S_PLUGIN, TEXT_PLUGIN,
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    obs_property_list_add_string(
        vst3list, obs_module_text("VST3.Select"), "");

    if (g_scanner_list_) {
        for (const auto &plugin : g_scanner_list_->pluginList) {
            const bool multi =
                g_scanner_list_->moduleHasMultipleClasses(
                    plugin.path);
            std::string display = multi
                ? plugin.name + " (" + plugin.pluginName + ")"
                : plugin.name;
            std::string value = plugin.id;
            obs_property_list_add_string(
                vst3list, display.c_str(), value.c_str());
        }
    }

    obs_property_t *gui = obs_properties_add_button2(
        props, S_EDITOR, obs_module_text(TEXT_EDITOR),
        vst3_show_gui_callback, nullptr);
    obs_property_set_visible(
        gui, !vd->noview.load(std::memory_order_relaxed) &&
             !vd->last_init_failed);

    sources = obs_properties_add_list(
        props, S_SIDECHAIN_SOURCE, TEXT_SIDECHAIN_SOURCE,
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_set_visible(sources, !vd->last_init_failed);

    obs_property_set_modified_callback2(
        vst3list, on_vst3_changed_cb, data);

    obs_property_t *noview = obs_properties_add_text(
        props, S_NOGUI, TEXT_NOGUI, OBS_TEXT_INFO);
    obs_property_text_set_info_type(noview, OBS_TEXT_INFO_WARNING);
    obs_property_set_visible(
        noview,
        vd->noview.load(std::memory_order_relaxed) &&
            !vd->last_init_failed);

    if (vd->last_init_failed) {
        obs_property_t *err = obs_properties_add_text(
            props, S_ERR, TEXT_ERR, OBS_TEXT_INFO);
        obs_property_text_set_info_type(err, OBS_TEXT_INFO_ERROR);
    }

    if (!g_vst3_scan_done_.load(std::memory_order_relaxed)) {
        obs_property_t *scan_err = obs_properties_add_text(
            props, S_SCAN, TEXT_SCAN, OBS_TEXT_INFO);
        obs_property_text_set_info_type(
            scan_err, OBS_TEXT_INFO_ERROR);
    }

    return props;
}
