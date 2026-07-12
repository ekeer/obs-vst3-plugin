# obs-vst3-plugin

VST3 plugin host for OBS Studio with **chain/graph routing** capability.

Based on [pkviet's obs-vst3 PR #12752](https://github.com/obsproject/obs-studio/pull/12752) with code refactoring per OBS maintainer PatTheMav's review, plus extended plugin chain routing.

## Features

### Single VST3 Filter (`obs-vst3`)
- Load individual VST3 audio effects as OBS filters
- Sidechain support (mono/stereo)
- Plugin state persistence across sessions
- Hot-swappable plugins
- Cross-platform: Windows, macOS, Linux

### VST3 Graph Filter (`obs-vst3-graph`) ✨
- **Plugin chain**: Chain multiple VST3 plugins in series
- **Graph routing**: Process audio through a directed graph of VST3 plugins
- **Save/Load chains**: Persist plugin chain configurations as JSON files
- **Per-node enable/disable**: Bypass individual nodes in the chain
- **Intended for**: Complex signal processing chains (EQ → Comp → Limiter → etc.)

## Architecture

```
                    ┌─────────────────────────────┐
                    │    obs-vst3-plugin           │
                    │                              │
                    │  ┌───────────────────────┐   │
                    │  │  VST3 Filter (single) │   │
                    │  └──────────┬────────────┘   │
                    │             │                 │
                    │  ┌──────────▼────────────┐   │
                    │  │  VST3 Graph Filter    │   │
                    │  │  (plugin chain)       │   │
                    │  └───────────────────────┘   │
                    │                              │
                    │  ┌───────────────────────┐   │
                    │  │  VST3 Host (SDK)      │   │
                    │  └───────────────────────┘   │
                    └─────────────────────────────┘
```

### File Structure

```
obs-vst3/
├── src/
│   ├── plugin-main.cpp           # Module entry point
│   ├── obs-vst3-module.cpp       # Host lifecycle, scanning, cache
│   ├── vst3-filter.cpp           # Single VST3 filter lifecycle
│   ├── vst3-filter-audio.cpp     # Audio processing pipeline
│   ├── vst3-filter-properties.cpp # Properties panel
│   ├── vst3-filter-save.cpp      # State persistence
│   ├── vst3-filter-sidechain.cpp # Sidechain handling
│   ├── VST3Plugin.cpp            # VST3 plugin wrapper
│   ├── VST3Scanner.cpp           # Common scanning logic
│   ├── VST3Scanner_Windows.cpp   # Windows search paths
│   ├── VST3Scanner_macOS.cpp     # macOS search paths
│   ├── VST3Scanner_Linux.cpp     # Linux search paths
│   ├── VST3HostApp.cpp           # IHostApplication
│   ├── VST3ComponentHolder.cpp   # Edit handler
│   └── VST3Graph.cpp             # Chain/graph routing
├── include/
│   ├── obs-vst3.h                # Core struct definitions
│   ├── vst3-filter.h             # Filter lifecycle declarations
│   ├── vst3-filter-audio.h       # Audio processing declarations
│   ├── vst3-filter-properties.h  # Properties declarations
│   ├── vst3-filter-sidechain.h   # Sidechain declarations
│   ├── VST3Plugin.h
│   ├── VST3Scanner.h
│   ├── VST3HostApp.h
│   ├── VST3ComponentHolder.h
│   ├── VST3EditorWindow.h
│   └── VST3Graph.h               # Graph routing declarations
├── cmake/
│   ├── FindVST3SDK.cmake         # VST3 SDK finder
│   └── windows/                  # Windows resource files
├── data/locale/                  # Translation files
├── editor/                       # Platform-specific editor windows
└── CMakeLists.txt
```

## Building

### Prerequisites

- CMake 3.28+
- Qt 6.x
- OBS Studio 31.1+ (headers/libs)
- Steinberg VST3 SDK (included via obs-deps or manual path)

### Windows (Visual Studio 2022)

```powershell
# Install obs-deps (includes VST3 SDK)
# Set VST3SDK_PATH or let CMake find it

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
```

### macOS

```bash
brew install cmake qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

### Linux

```bash
sudo apt install cmake build-essential qt6-base-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

## Installation

Copy the built plugin to your OBS plugins directory:

- **Windows**: `%APPDATA%\obs-studio\plugins\obs-vst3\bin\64-bit\`
- **macOS**: `~/Library/Application Support/obs-studio/plugins/obs-vst3/bin/`
- **Linux**: `~/.config/obs-studio/plugins/obs-vst3/bin/64-bit/`

## Usage

1. Open OBS Studio
2. Right-click an audio source → **Filters**
3. Click **+** → **VST3 Plugin** (single plugin) or **VST3 Graph** (plugin chain)
4. Select your VST3 plugin from the list
5. Click **Open Plugin Settings** to configure

For the Graph filter:
- Add multiple VST3 plugins to create a processing chain
- Drag to reorder
- Enable/disable individual nodes
- Save/load chain configurations

## License

GNU General Public License v3.0

## Credits

- [pkviet](https://github.com/pkviet) - Original obs-vst3 implementation
- [OBS Studio](https://github.com/obsproject/obs-studio) - Open Broadcaster Software
- [Steinberg Media Technologies](https://www.steinberg.net/) - VST3 SDK
