cmake_minimum_required(VERSION 3.25)
project(hyrule_field_passer_prototype)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DUSK_DIR "" CACHE PATH "Path to the Dusklight source checkout")
    if (NOT DUSK_DIR)
        message(FATAL_ERROR "Set DUSK_DIR to a complete Dusklight source checkout")
    endif ()
    add_subdirectory("${DUSK_DIR}/sdk" dusk-sdk EXCLUDE_FROM_ALL)
endif ()

# The current installable prototype is an asset overlay. Native behavior hooks in
# experimental/ are deliberately not compiled until they can be built and validated.
add_mod(hyrule_field_passer_prototype
    MOD_JSON mod.json
    OVERLAY_DIR overlay
)

