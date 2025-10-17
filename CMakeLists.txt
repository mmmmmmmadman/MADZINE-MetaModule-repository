cmake_minimum_required(VERSION 3.22)

# Only include SDK when not building as part of simulator
if(NOT DEFINED METAMODULE_SDK_DIR)
    include(../metamodule-plugin-sdk/plugin.cmake)
endif()

project(MADZINE
    VERSION 2.3.0
    DESCRIPTION "MADZINE Modules for MetaModule"
    LANGUAGES C CXX ASM
)

add_library(MADZINE STATIC)
target_include_directories(MADZINE PRIVATE src)

target_sources(MADZINE PRIVATE
    src/plugin.cpp
    src/SwingLFO.cpp
    src/EuclideanRhythm.cpp
    src/ADGenerator.cpp
    src/Pinpple.cpp
    src/MADDY.cpp
    src/PPaTTTerning.cpp
    src/TWNC.cpp
    src/TWNCLight.cpp
    src/QQ.cpp
    src/Observer.cpp
    src/TWNC2.cpp
    src/U8.cpp
    src/YAMANOTE.cpp
    src/Obserfour.cpp
    src/KIMO.cpp
    src/Quantizer.cpp
    src/EllenRipley.cpp
    src/MADDYPlus.cpp
    src/EnvVCA6.cpp
    src/NIGOQ.cpp
    src/Runshow.cpp
)

# Create plugin or assets depending on build mode
if(NOT DEFINED METAMODULE_SDK_DIR)
    # Building standalone plugin
    create_plugin(
        SOURCE_LIB      MADZINE
        PLUGIN_NAME     MADZINE
        PLUGIN_JSON     ${CMAKE_CURRENT_LIST_DIR}/plugin.json
        SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
        DESTINATION     ${CMAKE_CURRENT_LIST_DIR}/../metamodule-plugins/
    )
else()
    # Building as part of simulator - just create assets target
    add_custom_command(
        OUTPUT "${ASSET_DIR}/MADZINE"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_LIST_DIR}/assets" "${ASSET_DIR}/MADZINE"
        COMMENT "Copying MADZINE assets to ${ASSET_DIR}/MADZINE"
        VERBATIM
    )

    add_custom_target(MADZINE-assets ALL
        DEPENDS "${ASSET_DIR}/MADZINE"
    )
endif()