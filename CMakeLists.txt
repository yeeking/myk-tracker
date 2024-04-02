cmake_minimum_required(VERSION 3.10)

# Set your project name
project(MykTracker)

# Specify the C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Find the ncurses package
find_package(Curses REQUIRED)

# Include the ncurses headers
include_directories(${CURSES_INCLUDE_DIR})

add_executable(MykTracker src/gui.cpp src/main.cpp)
add_executable(CurTest src/curtest.cpp)

# Link the ncurses library with your executable
target_link_libraries(${PROJECT_NAME} ${CURSES_LIBRARIES} panel)
target_link_libraries(CurTest ${CURSES_LIBRARIES})
