search synth, filter
let color = #336699
solid(
  // Source positions survive argument comments.
  color: #336699, alpha: -0.2 + Math.PI / 4,
).write(o0)
read(tex: o0).rotate(angle: 0.1).write(o1)
render(o1)
