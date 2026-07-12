cmake_minimum_required(VERSION 3.28)

project(obs-vst3-plugin VERSION 1.0.0
  DESCRIPTION "VST3 Plugin Host with Chain/Graph Routing for OBS Studio"
  LANGUAGES C CXX)

# Default to build as standalone OBS plugin
option(BUILD_WITH_OBS "Build obs-vst3 as part of OBS tree" OFF)

if(BUILD_WITH_OBS)
  # Building within the OBS source tree
  add_subdirectory(obs-vst3)
else()
  # Standalone build - requires OBS SDK and deps
  # User must set:
  #   - OBS_SDK_DIR or OBS_STUDIO_BUILD_DIR for OBS headers/libs
  #   - VST3SDK_PATH for the VST3 SDK

  find_package(libobs REQUIRED)
  find_package(Qt6 REQUIRED Widgets)
  find_package(VST3SDK REQUIRED)

  add_subdirectory(obs-vst3)
endif()
