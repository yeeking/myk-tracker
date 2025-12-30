# myk-tracker: curse/ JUCE GUI version freeze

Music sequencer with tracker-like properties

## Building it

There is a juce version and a conole version. 

Tested on Linux and macos:

```
# linux only
# along with the usual build tools: 
sudo apt install libncurses-dev 

# linux and macos
# to build with cmake
cmake -B build .
cmake --build build --config Release
```

Then to run it:


```
# For the command-line version: 
# macos, if you downloaded a release, you need to set the lib path: 
# disable security if you did not build it yourself: 
sudo spctl --master-disable
export DYLD_LIBRARY_PATH=.:./rtmidi:$DYLD_LIBRARY_PATH
./build/MykTracker 
# later, can re-enable security 
sudo spctl --master-enable
```


Or for the juce version,  just right click and open the .app which will be built into this folder probably:

```
open build/myk-tracker-plug_artefacts/Standalone
```






