search synth, filter, mixer
noise().write(o0)
solid().blendMode(tex: read(o0).invert()).write(o1)
render(o1)
