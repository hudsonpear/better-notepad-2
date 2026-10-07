cmake_minimum_required(VERSION 3.21)
project(BetterNotepad VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets PrintSupport Concurrent Network Svg)

qt_add_executable(BetterNotepad WIN32
    src-cpp/native_main.cpp
    src-cpp/resources.qrc
)
if(WIN32)
    target_sources(BetterNotepad PRIVATE src-cpp/app.rc)
endif()

target_link_libraries(BetterNotepad PRIVATE
    Qt6::Widgets Qt6::PrintSupport Qt6::Concurrent Qt6::Network Qt6::Svg
)

if(WIN32)
    target_link_libraries(BetterNotepad PRIVATE shell32)
endif()

