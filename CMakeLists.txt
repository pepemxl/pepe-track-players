cmake_minimum_required(VERSION 3.21)
project(pepe_track_players LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- Qt 6 (Quick/QML app) ---
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Qml QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

# --- OpenCV ---
# On Windows, set OpenCV_DIR via -DOpenCV_DIR=... or the OpenCV_DIR env var
# (e.g. the vcpkg_installed share/opencv4 dir, or a vcpkg toolchain).
if(DEFINED ENV{OpenCV_DIR} AND NOT DEFINED OpenCV_DIR)
    set(OpenCV_DIR "$ENV{OpenCV_DIR}" CACHE PATH "OpenCV installation directory")
endif()
find_package(OpenCV REQUIRED)

set(SOURCES
    src/main.cpp
    src/AppController.cpp
    src/AppController.h
    src/VideoEngine.cpp
    src/VideoEngine.h
    src/FrameProvider.cpp
    src/FrameProvider.h
    src/RosterModel.cpp
    src/RosterModel.h
    src/MatchMetadata.cpp
    src/MatchMetadata.h
    src/TagsModel.cpp
    src/TagsModel.h
    src/HomographyManager.cpp
    src/HomographyManager.h
    src/HomographyWorker.cpp
    src/HomographyWorker.h
    src/TracksModel.cpp
    src/TracksModel.h
    src/TrackingManager.cpp
    src/TrackingManager.h
    src/YoloDetector.cpp
    src/YoloDetector.h
    src/MatchManager.cpp
    src/MatchManager.h
    src/VideoOpsWorker.cpp
    src/VideoOpsWorker.h
    src/LineupExtractor.cpp
    src/LineupExtractor.h
)

qt_add_executable(pepe_track_players WIN32 ${SOURCES})

add_subdirectory(qml)

target_include_directories(pepe_track_players PRIVATE ${OpenCV_INCLUDE_DIRS} src)

target_link_libraries(pepe_track_players PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    pepetrack_qmlplugin
    ${OpenCV_LIBS}
)

if(MSVC)
    target_compile_options(pepe_track_players PRIVATE /W4 /permissive-)
else()
    target_compile_options(pepe_track_players PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Ship the detection model and helper scripts next to the binary.
add_custom_command(TARGET pepe_track_players POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/models"
            "$<TARGET_FILE_DIR:pepe_track_players>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts"
            "$<TARGET_FILE_DIR:pepe_track_players>/scripts"
    COMMENT "Copying detection models and scripts"
)

# Copy required Qt runtime DLLs (incl. QML imports) alongside the binary on Windows.
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET pepe_track_players POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                    "$<TARGET_FILE:pepe_track_players>"
            COMMENT "Deploying Qt runtime DLLs"
        )
    endif()
endif()
