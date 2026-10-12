search synth, points, render

noise(seed: 1, speed: 0).pointsEmit(stateSize: x64).buddhabrot().pointsRender(density: 5, inputIntensity: 0, intensity: 100).write(o0)
render(o0)
