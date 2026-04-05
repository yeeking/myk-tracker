# MYK Tracker GL

MYK Tracker GL is a keyboard-driven music tracker built with JUCE. It combines classic step sequencing with machine stacks, internal instruments, arpeggiators, and audio effects, so you can build patterns, arrange them into songs, and perform most editing directly from the keyboard.

## Main Pages

- `Song` page: arrange sequence sets into a song and choose how many beats each row runs before switching.
- `Sequence` page: browse sequences and steps, mute/arm tracks, and move around the current pattern.
- `Step` page: edit the command rows inside a single step, including notes, velocity, duration, and probability.
- `Machine` page: inspect and configure the machine stack for the current track, including instruments and effects.
- `Machine Detail` page: open the focused machine's compact tracker UI for detailed parameter editing.
- `Sequence Config` page: edit per-sequence settings such as machine routing and timing.
- `Reset / Quit` confirmation page: confirm tracker reset and, in standalone builds, quit.

## Keyboard Shortcuts

- `Space`: start/stop playback.
- `1`: go to Song page.
- `2`: go to Sequence page.
- `3`: go to Step page.
- `4`: go to Machine page.
- `5`: open Machine Detail from anywhere, or cycle to the next machine detail while already in Machine page detail view.
- `6`: go to Sequence Config page.
- `Enter`: activate/click the current item.
- `Esc`: dismiss the current transient UI if a machine owns one.
- `Arrow keys`: move the editor cursor.
- `Page Up` / `Page Down`: jump by larger row amounts on the Machine page.
- `Tab`: next step, or next machine detail when already editing a machine detail view.
- `Backspace`: reset or clear the current item. On machine pages, this first tries the machine-specific clear action.
- `q`: mute/unmute the current sequence.
- `e`: arm the current sequence for note entry.
- `r`: rewind transport.
- `-`: remove a row or entry where supported.
- `=`: add a row or entry where supported.
- `[` / `]`: decrement/increment the current value.
- `,` / `.`: octave down / octave up for note entry.
- `_` / `+`: decrease / increase BPM by 1.
- Piano note keys: enter notes into steps and machine note cells using the current octave.
- Chord shortcut keys on the Step page:
  - `q`: major triad
  - `w`: minor triad
  - `e`: dominant 7
  - `r`: major 7
  - `t`: minor 7
  - `y`: diminished 7
  - `u`: half-diminished
  - `i`: sus4
  - `o`: major 9
  - `p`: minor 9
- `Shift+C`: toggle the internal clock on/off.
- `Ctrl+R`: open tracker reset confirmation.
- Standalone only:
  - `Ctrl+Q`: open quit confirmation.
  - `Ctrl+P`: open audio/MIDI device settings.

## Developer Build

Clone the tracker repo, clone JUCE into `libs/JUCE`, then configure and build with CMake.

```bash
git clone <myk-tracker-repo-url>
cd myk-tracker
git clone https://github.com/juce-framework/JUCE.git libs/JUCE
cmake -B build .
cmake --build build --target myk-tracker-plug_Standalone
```

The standalone app is produced under:

```bash
build/myk-tracker-plug_artefacts/Debug/Standalone/
```
