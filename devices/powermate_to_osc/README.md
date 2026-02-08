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
