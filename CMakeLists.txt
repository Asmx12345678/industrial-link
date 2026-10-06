cmake_minimum_required(VERSION 3.20)
project(IndustrialLink VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(INDUSTRIAL_LINK_BUILD_QT_CLIENT "Build the Qt6 monitor client" ON)
find_package(SQLite3 REQUIRED)

add_library(industrial_link_core
    gateway/src/gateway.cpp
    gateway/src/plugin_manager.cpp
    gateway/src/storage.cpp
)
target_include_directories(industrial_link_core PUBLIC gateway/include)
if(TARGET SQLite3::SQLite3)
    target_link_libraries(industrial_link_core PRIVATE SQLite3::SQLite3)
else()
    # CMake 3.x exposes the historical target name; CMake 4.x prefers the one above.
    target_link_libraries(industrial_link_core PRIVATE SQLite::SQLite3)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(industrial_link_core PRIVATE dl pthread)
endif()

add_executable(industrial-gateway gateway/src/main.cpp)
target_link_libraries(industrial-gateway PRIVATE industrial_link_core)

add_library(vendor_pipe MODULE plugins/sample_adapter.cpp)
target_include_directories(vendor_pipe PRIVATE gateway/include)
set_target_properties(vendor_pipe PROPERTIES PREFIX "lib")

if(INDUSTRIAL_LINK_BUILD_QT_CLIENT)
    find_package(Qt6 6.4 COMPONENTS Widgets Network REQUIRED)
    qt_standard_project_setup()
    qt_add_executable(industrial-monitor
        monitor-client/main.cpp
        monitor-client/mainwindow.cpp
        monitor-client/mainwindow.hpp
    )
    target_link_libraries(industrial-monitor PRIVATE Qt6::Widgets Qt6::Network)
endif()

install(TARGETS industrial-gateway vendor_pipe RUNTIME DESTINATION bin LIBRARY DESTINATION lib)
