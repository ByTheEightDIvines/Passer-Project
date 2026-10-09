cmake_minimum_required(VERSION 3.25)
project(hyrule_field_passer_prototype CXX)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DUSK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dusklight-capture-source" CACHE PATH
        "Path to the Dusklight source checkout")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory("${DUSK_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)
endif ()

add_mod(hyrule_field_passer_prototype
    FEATURES game
    SOURCES src/passer_prototype.cpp
    MOD_JSON mod.json
    OVERLAY_DIR overlay
    BUNDLE
)


