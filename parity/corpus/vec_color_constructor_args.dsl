// vec3()/vec4() color constructors (reference 688794ac): a color parameter
// accepts vec3(x, y, z) and vec4(x, y, z, w), preserving the supplied
// components independently of the default's length (solid's color default
// [0.5, 0.5, 0.5] is three components). Wrong component counts and wrong
// constructor names keep the earlier dispatch: the color resolver for color
// params (S002 + the default). A non-Number argument inside vec3() takes the
// constructor resolver's S002 branch and that component resolves to 0.
// (A kwargs form such as vec3(1, 2, 3, bogus: 1) is upstream's experimental
// lang.mixedArguments grammar and is not ported; the port's parser rejects
// mixed positional and keyword arguments.)
search synth

solid(color: vec3(0.3, 0.2, 0.25)).write(o0)
solid(color: vec4(0.3, 0.2, 0.25, 0.4)).write(o1)
solid(color: vec3(0.3, 0.2, 0.25, 0.4)).write(o2)
solid(color: vec4(0.3, 0.2, 0.25, 0.4, 1)).write(o3)
solid(color: vec3(0.3, x, 0.25)).write(o4)

render(o0)
