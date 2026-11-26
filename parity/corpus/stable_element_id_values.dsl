search synth, filter
let label = "variableId"
solid(color: #336699, _id: label).write(o0, _id: 7)
read(tex: o0, _id: "bad-id").invert(_id: "Good_id2").write(o1)
render(o1)
