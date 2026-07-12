# FindVST3SDK.cmake
# Find the Steinberg VST3 SDK for building obs-vst3.
#
# This module searches for VST3 SDK in the following order:
#   1. VST3SDK_PATH environment variable
#   2. VST3SDK_PATH CMake variable
#   3. obs-deps installation directory
#   4. Common install locations
#
# If found, sets:
#   VST3SDK_FOUND        - TRUE if found
#   VST3SDK_PATH         - Path to the VST3 SDK root
#   VST3SDK_INCLUDE_DIRS - Include directories
#   VST3SDK_REQUIRED_FILES - List of SDK source files needed
#
# This module is intended to be used with obs-deps which provides
# the VST3 SDK headers and sources. See:
#   https://github.com/obsproject/obs-deps

# Search order
if(DEFINED ENV{VST3SDK_PATH})
  set(_VST3SDK_PATH_CANDIDATE "$ENV{VST3SDK_PATH}")
elseif(DEFINED VST3SDK_PATH)
  set(_VST3SDK_PATH_CANDIDATE "${VST3SDK_PATH}")
elseif(DEFINED ENV{OBS_DEPS_PATH})
  set(_VST3SDK_PATH_CANDIDATE "$ENV{OBS_DEPS_PATH}/include/vst3sdk")
else()
  # Common install locations
  foreach(_dir
    "C:/Program Files/Steinberg/VST3SDK"
    "/Library/Audio/Plug-Ins/VST3SDK"
    "/usr/local/include/vst3sdk"
    "/usr/include/vst3sdk"
    "$ENV{LOCALAPPDATA}/vst3sdk"
  )
    if(EXISTS "${_dir}/pluginterfaces/vst/ivstcomponent.h")
      set(_VST3SDK_PATH_CANDIDATE "${_dir}")
      break()
    endif()
  endforeach()
endif()

# Validate the candidate
if(_VST3SDK_PATH_CANDIDATE AND
   EXISTS "${_VST3SDK_PATH_CANDIDATE}/pluginterfaces/vst/ivstcomponent.h")
  set(VST3SDK_PATH "${_VST3SDK_PATH_CANDIDATE}" CACHE PATH "Path to VST3 SDK")
  set(VST3SDK_FOUND TRUE)

  set(VST3SDK_INCLUDE_DIRS
    "${VST3SDK_PATH}"
    "${VST3SDK_PATH}/base"
    "${VST3SDK_PATH}/pluginterfaces"
    "${VST3SDK_PATH}/public.sdk/source/vst"
    "${VST3SDK_PATH}/public.sdk/source/vst/hosting"
    "${VST3SDK_PATH}/public.sdk/source/vst/utility"
  )

  # List of source files required from the SDK
  set(VST3SDK_REQUIRED_FILES
    "public.sdk/source/vst/hosting/module.cpp"
    "public.sdk/source/vst/hosting/eventlist.cpp"
    "public.sdk/source/vst/hosting/parameterchanges.cpp"
    "public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
    "public.sdk/source/vst/hosting/connectionpoint.cpp"
    "public.sdk/source/vst/hosting/hostclasses.cpp"
    "public.sdk/source/vst/utility/stringconvert.cpp"
    "public.sdk/source/vst/utility/uidutility.cpp"
    "public.sdk/source/common/threadchecker.cpp"
  )

  # Platform-specific SDK sources
  if(WIN32)
    list(APPEND VST3SDK_REQUIRED_FILES
      "public.sdk/source/vst/hosting/module_win32.cpp"
    )
  elseif(APPLE)
    list(APPEND VST3SDK_REQUIRED_FILES
      "public.sdk/source/vst/hosting/module_mac.mm"
      "public.sdk/source/common/threadchecker_mac.mm"
    )
  elseif(UNIX)
    list(APPEND VST3SDK_REQUIRED_FILES
      "public.sdk/source/vst/hosting/module_linux.cpp"
    )
  endif()

  message(STATUS "Found VST3 SDK: ${VST3SDK_PATH}")

  # Create an imported target for convenience
  if(NOT TARGET VST3SDK::sdk)
    add_library(VST3SDK::sdk INTERFACE IMPORTED)
    target_include_directories(VST3SDK::sdk INTERFACE ${VST3SDK_INCLUDE_DIRS})
  endif()

  mark_as_advanced(VST3SDK_PATH)
else()
  set(VST3SDK_FOUND FALSE)
  if(NOT VST3SDK_FIND_QUIETLY)
    message(STATUS "VST3 SDK not found. Set VST3SDK_PATH to point to the SDK.")
  endif()
endif()
