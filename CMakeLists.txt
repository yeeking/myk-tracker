cmake_minimum_required(VERSION 3.10)

# Set your project name
project(MykTracker)

set(CMAKE_CXX_STANDARD 11)

# Specify the C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Find the ncurses package
find_package(Curses REQUIRED)

# Include the ncurses headers
include_directories(${CURSES_INCLUDE_DIR})


# build the chord utils
# add_library(chordutil-lib src/ChordUtils.cpp)
# build the sequencer library
add_library(seq-lib src/Sequencer.cpp src/SequencerEditor.cpp src/SequencerCommands.cpp)
# build the midi utils
# add_library(midi-lib src/MidiUtils.cpp)

add_executable(MykTracker src/gui.cpp src/main.cpp)
add_executable(CurTest src/gui.cpp src/curtest.cpp)

# Link the ncurses library with your executable
target_link_libraries(${PROJECT_NAME} seq-lib ${CURSES_LIBRARIES} panel)
target_link_libraries(CurTest seq-lib ${CURSES_LIBRARIES} panel)
