search synth, filter, render

noise(seed: 1, scaleX: 50, scaleY: 50).loopBegin(alpha: 50).blur(radiusX: 2, radiusY: 2).loopEnd().write(o0)
render(o0)
