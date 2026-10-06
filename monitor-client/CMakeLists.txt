cmake_minimum_required(VERSION 3.20)
project(IndustrialLinkMonitor LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()
qt_add_executable(industrial-monitor main.cpp mainwindow.cpp mainwindow.hpp)
target_link_libraries(industrial-monitor PRIVATE Qt6::Widgets Qt6::Network)

