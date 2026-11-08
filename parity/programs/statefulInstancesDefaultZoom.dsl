search synth, mixer

reactionDiffusion(zoom: x4)
  .blendMode(tex: reactionDiffusion())
  .blendMode(tex: reactionDiffusion(zoom: x16))
  .write(o0)

render(o0)
