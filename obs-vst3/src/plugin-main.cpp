/******************************************************************************
    Copyright (C) 2025-2026 pkv <pkv@obsproject.com>
    This file is part of obs-vst3.
    It uses the Steinberg VST3 SDK, which is licensed under MIT license.
    See https://github.com/steinbergmedia/vst3sdk for details.
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "vst3-filter.h"
#include "VST3Graph.h"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-vst3", "en-US")

bool obs_module_load(void)
{
    if (!load_host()) {
        blog(LOG_WARNING, "[obs-vst3] Failed to load VST3 host");
        return false;
    }

    retrieve_vst3_list();

    // Register single VST3 filter
    register_vst3_source();

    // Register VST3 graph filter (plugin chain)
    register_vst3_graph_source();

    blog(LOG_INFO, "[obs-vst3] Module loaded (VST3 + Graph)");
    return true;
}

void obs_module_unload(void)
{
    free_vst3_list();
    unload_host();
    blog(LOG_INFO, "[obs-vst3] Module unloaded");
}
