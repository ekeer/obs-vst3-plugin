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

#include "VST3Scanner.h"

#include <string>
#include <vector>

// Linux VST3 search paths
std::vector<std::string> VST3Scanner::getDefaultSearchPaths()
{
    std::vector<std::string> paths;

    // System-wide (per VST3 specification)
    paths.push_back("/usr/lib/vst3");
    paths.push_back("/usr/local/lib/vst3");

    // User-specific
    const char *home = getenv("HOME");
    if (home) {
        std::string path = home;
        path += "/.vst3";
        paths.push_back(path);
    }

    // Flatpak applications
    const char *xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data) {
        std::string path = xdg_data;
        path += "/vst3";
        paths.push_back(path);
    }

    return paths;
}
