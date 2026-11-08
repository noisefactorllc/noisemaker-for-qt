search synth, mixer

perlin()
  .blendMode(tex: reactionDiffusion(zoom: x4))
  .blendMode(tex: reactionDiffusion(zoom: x16))
  .write(o0)

render(o0)
