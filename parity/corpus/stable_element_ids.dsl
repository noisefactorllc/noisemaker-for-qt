search synth, filter
solid(color: #336699, _id: "colorA").write(o0, _id: "outputA")
read(tex: o0, _id: "inputA").invert(_id: "invertA").write(o1, _id: "outputB")
render(o1)
