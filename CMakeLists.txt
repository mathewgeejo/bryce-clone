cmake_minimum_required(VERSION 3.16)
project(Canyon3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
add_library(canyon_core STATIC
    src/image.cpp
    src/mesh.cpp
    src/presets.cpp
    src/render.cpp
    src/scene.cpp
    src/terrain.cpp
    src/vmath.cpp
)
target_link_libraries(canyon_core PUBLIC Threads::Threads)

if(WIN32)
    add_executable(Canyon3D WIN32 src/main.cpp src/desktop_win.cpp)
    target_link_libraries(Canyon3D PRIVATE user32 gdi32 comdlg32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.24)
    add_executable(Canyon3D src/main.cpp src/desktop_gtk.cpp)
    target_link_libraries(Canyon3D PRIVATE PkgConfig::GTK3)
else()
    message(FATAL_ERROR "The desktop application supports Windows and Linux.")
endif()
target_link_libraries(Canyon3D PRIVATE canyon_core)
install(TARGETS Canyon3D RUNTIME DESTINATION bin)

option(BUILD_TESTING "Build the renderer regression checks" OFF)
include(CTest)
if(BUILD_TESTING)
    set(test_assets "${CMAKE_CURRENT_BINARY_DIR}/build/t")
    file(MAKE_DIRECTORY "${test_assets}")
    configure_file(build/t/ex1.bmp "${test_assets}/ex1.bmp" COPYONLY)
    add_executable(canyon_checks build/t/test.cpp)
    target_link_libraries(canyon_checks PRIVATE canyon_core)
    add_test(NAME core COMMAND canyon_checks)
    add_test(NAME headless_render COMMAND Canyon3D --render "${test_assets}/headless smoke.bmp" 64 40)
    set_tests_properties(headless_render PROPERTIES ENVIRONMENT "DISPLAY=;WAYLAND_DISPLAY=")
endif()
