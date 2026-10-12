search synth, mixer, filter

testPattern(pattern: colorBars).alphaMask(tex: solid(color: #808080), maskMode: true).grade(exposure: 0.2, contrast: 0.15, fadedFilm: 0.15, preset: warmFilm, alpha: 0.6, vignetteAmount: 0.4).write(o0)
render(o0)
