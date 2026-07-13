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

// Windows VST3 search paths
std::vector<std::string> VST3Scanner::getDefaultSearchPaths()
{
    std::vector<std::string> paths;

    // Standard VST3 locations on Windows
    const char *env = nullptr;
    size_t len = 0;

    // Per-user installs
    if (_dupenv_s(&env, &len, "APPDATA") == 0 && env) {
        std::string path = env;
        path += "\\VST3";
        paths.push_back(path);
        path = env;
        path += "\\..\\Local\\VST3";
        paths.push_back(path);
        free((void *)env);
    }

    // System-wide installs (64-bit plugins on 64-bit OS)
    paths.push_back("C:\\Program Files\\Common Files\\VST3");

    // 32-bit plugins on 64-bit OS
    paths.push_back("C:\\Program Files (x86)\\Common Files\\VST3");

    // Additional common locations
    paths.push_back("C:\\Program Files\\Steinberg\\VST3");

    return paths;
}
