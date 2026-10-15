# powermate_to_osc
Code that talks to ancient Griffin Powermate USB dial thingy

```
gcc powermate_print.c
./a.out
```

Or, if you have some supercollider code like this:

```

SynthDef("sine2", {
	var f, c;
	f = In.kr(1);
	c = SinOsc.ar([f, f]);
	Out.ar(0, c);
}).add;

a = Synth("sine2");
// the power mate can do this:
s.sendMsg('/c_set', 1, 200);

```

```
g++ powermate_to_osc.cpp
./a.out
```

## MYK Tracker zoom

`powermate_to_zoom.cpp` sends PowerMate dial rotation to the tracker OSC
receiver (`127.0.0.1:9001` by default). Positive rotation sends `/zoom_in`;
negative rotation sends `/zoom_out`. Each message includes the current mouse
pointer location, normalized to the X11 desktop, so a fullscreen tracker zooms
around the pointer.

```sh
g++ -std=c++17 -O2 -Wall -Wextra powermate_to_zoom.cpp -o powermate_to_zoom -lX11
./powermate_to_zoom
```

Or build the utility independently with CMake:

```sh
cmake -S . -B build
cmake --build build
./build/powermate_to_zoom
```

Optional arguments are `event-device`, `host`, and `port`:

```sh
DISPLAY=:0 ./powermate_to_zoom /dev/input/by-id/usb-Griffin_Technology__Inc._Griffin_PowerMate-event-if00 127.0.0.1 9001
```

The PowerMate event device normally requires membership of the `input` group
or a suitable udev rule. If no X11 display is available, the utility warns and
uses the centre of the screen as its zoom reference.
