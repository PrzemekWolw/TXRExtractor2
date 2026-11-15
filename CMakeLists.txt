cmake_minimum_required(VERSION 3.21)
if(NOT ANDROID)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(TXRExtractor2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION LIMIT_COUNT 1)

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9b GIT_SHALLOW TRUE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib GIT_REPOSITORY https://github.com/madler/zlib.git GIT_TAG v1.3.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw imgui nlohmann_json zlib)

set(APP_SOURCES
    src/main.cpp
    src/ui.cpp
    src/theme.cpp
    src/task.cpp
    src/platform.cpp
    src/settings.cpp
    src/i18n.cpp
    src/ucl.cpp
    src/png.cpp
    src/xpr.cpp
    src/glb.cpp
    src/itc_mesh.cpp
    src/wmn.cpp
    src/package.cpp
    src/selftest.cpp
)
set(IMGUI_CORE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(TXRExtractor2 WIN32 ${APP_SOURCES} ${IMGUI_CORE})
target_include_directories(TXRExtractor2 PRIVATE src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(TXRExtractor2 PRIVATE APP_VERSION="${APP_VERSION}" IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(TXRExtractor2 PRIVATE glfw OpenGL::GL Threads::Threads nlohmann_json::nlohmann_json zlibstatic)
if(WIN32)
    target_compile_definitions(TXRExtractor2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(TXRExtractor2 PRIVATE dwmapi comdlg32)
    target_link_options(TXRExtractor2 PRIVATE /ENTRY:mainCRTStartup)
endif()
if(APPLE)
    target_compile_definitions(TXRExtractor2 PRIVATE GL_SILENCE_DEPRECATION)
endif()
if(MSVC)
    target_compile_definitions(TXRExtractor2 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(TXRExtractor2 PRIVATE /W4 /utf-8)
else()
    target_compile_options(TXRExtractor2 PRIVATE -Wall -Wextra)
endif()

foreach(dir fonts data)
    add_custom_command(TARGET TXRExtractor2 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}" "$<TARGET_FILE_DIR:TXRExtractor2>/${dir}")
endforeach()
