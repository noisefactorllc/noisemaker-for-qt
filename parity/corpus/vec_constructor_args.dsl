// vec2()/vec4() constructor arguments (reference 0553e242):
// vec2(x, y) on a vec2-typed param (media.imageSize) and vec4(x, y, z, w)
// on a color-typed param whose default is a 4-component array (the loader
// registers vec4 globals as `color`; synth.remap's zone*_bounds qualify).
// Wrong component counts and wrong constructor names keep the earlier
// dispatch: the numeric catch-all for vec2 params, the color resolver for
// color params. A non-Number argument inside the constructor (the bare
// Ident `x` below) takes the constructor resolver's S002 branch: the
// diagnostic names the argument and that component resolves to 0.
search synth

media(imageSize: vec2(64, 48)).write(o0)
media(imageSize: vec2(64, 48, 9)).write(o1)
remap(zone0_bounds: vec4(0.1, 0.2, 0.3, 0.4)).write(o2)
remap(zone0_bounds: vec4(0.1, 0.2, 0.3)).write(o3)
solid(color: vec2(1, 2)).write(o4)
remap(zone0_bounds: vec4(0.1, x, 0.3, 0.4)).write(o5)

render(o0)
