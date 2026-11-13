cmake_minimum_required(VERSION 3.26)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type")
endif ()

project(dynamic_travelers VERSION 0.3.0 LANGUAGES CXX)

# The Dusklight version to build against: a release tag (e.g. "v2.0.0") or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "v2.0.3")
endif ()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)

configure_file(mod.json.in "${CMAKE_CURRENT_BINARY_DIR}/mod.json" @ONLY NEWLINE_STYLE LF)

# Game-derived archives (tents, passer bodies) live in overlay/, which is kept out of the public repo.
# Local builds include it when present; CI builds the code-only bundle.
set(PASSER_OVERLAY_ARGS "")
if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/overlay")
    set(PASSER_OVERLAY_ARGS OVERLAY_DIR overlay)
endif ()

add_mod(dynamic_travelers
        FEATURES game
        SOURCES src/camp_cook.cpp src/camp_fire.cpp src/passer_prototype.cpp src/spawner_actor.cpp src/spot_picker.cpp src/tent_actor.cpp src/traveller_gear.cpp src/travellers.cpp
        MOD_JSON "${CMAKE_CURRENT_BINARY_DIR}/mod.json"
        ${PASSER_OVERLAY_ARGS}
)

# MSVC reports __cplusplus as 199711L unless told otherwise, which makes the Dolphin
# compatibility headers redefine nullptr and breaks the standard library.
if (MSVC)
    target_compile_options(dynamic_travelers PRIVATE /Zc:__cplusplus)
endif ()
