search synth3d, render
noise3d().write3d(vol0, geo0, _id: "volumeA")
read3d(tex3d: vol0, geo: geo0, _id: "volumeB").render3d(cameraPosition: [0, // origin
  0, 3,]).write(o0)
render(o0)
