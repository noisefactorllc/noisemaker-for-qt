search synth, filter, render

noise(seed: 2, scaleX: 50, scaleY: 50).loopBegin(alpha: 25).blur(radiusX: 3, radiusY: 3).loopEnd().write(o0)
render(o0)
