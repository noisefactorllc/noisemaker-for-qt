search synth
let channel = 5
let mode = midiMode.velocity
let floor = 0.1
let ceiling = 0.8
solid(alpha: midi(channel: channel, mode: mode, min: floor, max: ceiling), color: #336699).write(o0)
render(o0)
