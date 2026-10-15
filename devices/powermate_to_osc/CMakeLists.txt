cmake_minimum_required(VERSION 3.16)

project(powermate_to_osc LANGUAGES CXX)

find_package(X11 REQUIRED)

add_executable(powermate_to_zoom powermate_to_zoom.cpp)
target_compile_features(powermate_to_zoom PRIVATE cxx_std_17)
target_compile_options(powermate_to_zoom PRIVATE -Wall -Wextra)
target_link_libraries(powermate_to_zoom PRIVATE X11::X11)
