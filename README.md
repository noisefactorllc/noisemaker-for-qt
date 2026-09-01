<!-- repo-hero -->
<a href="https://noisemaker.app/"><img src="docs/hero.jpg" alt="Noisemaker for Qt" width="100%"></a>

<sub>Open source from <a href="https://noisefactor.io">Noise Factor</a> &middot; <a href="https://github.com/noisefactorllc">more projects</a></sub>

# Noisemaker for Qt

**Noisemaker** is a procedural visual engine: small text programs — chains of composable effects —
compile to a render graph and run live as animated GPU textures. **Noisemaker for Qt** brings that
same DSL compiler and effects library to Qt 6, as an idiomatic C++17 library (`noisemaker-qt`,
namespace `nm::`) plus an offscreen render CLI (`nm-render`) and a live `QOpenGLWidget` example,
executed via classic `QOpenGL*` with pixel-level parity against the reference engine.

## Family context

This port is one of several hand-ports of the same reference engine to different target platforms
— siblings targeting Unity (HLSL), Godot (GDShader), and TouchDesigner (GLSL) among them — and
shares their infrastructure: the engine-agnostic `reference/01–10` re-implementer specs, the
render-graph JSON contract (`docs/GRAPH-JSON-SCHEMA.md`), and the parity-harness design
(`parity/`). Unlike the HLSL/GDShader ports, this one shares the reference's actual shader
*language* — desktop GLSL is the same family as the reference's WebGL2 GLSL ES source, so this
port commits the reference GLSL byte-identical rather than re-deriving it (see
[ARCHITECTURE.md](ARCHITECTURE.md) "Shader corpus" and "Why classic OpenGL" for what that does and
doesn't buy in terms of parity risk).

## What's here

- **`libnoisemaker-qt`** (CMake target `noisemaker-qt`) — the DSL compiler
  (`qt/noisemaker/compiler/`: lexer → parser → validator → expander → resources → orchestrator)
  and the render-graph runtime (`qt/noisemaker/runtime/`: a `QOpenGL*` executor, `nm::Backend`).
  Qt 6 Core/Gui/OpenGL only — no Widgets/QML dependency.
- **`nm-render`** (`qt/tools/nm-render/`) — an offscreen render CLI: `--dsl <file>` (live compiler)
  or `--graph <json>` (a pre-exported graph) in, a PNG out. Drives the parity harness and doubles
  as a standalone batch renderer.
- **`examples/viewer`** — a minimal live `QOpenGLWidget` embedding: DSL in (the live compiler path,
  `nm::compileGraph`), an animated render out, at roughly 60fps. The smallest honest demonstration
  of embedding this library in a real Qt application. Widgets/OpenGLWidgets are a dependency of
  this example only, never of `libnoisemaker-qt` itself.

## Quickstart

Every command below was verified by literally running it before it was written down here.
The canonical build directory for the core library is `qt/build` (gitignored), and that's what
the commands below show; any out-of-tree build directory works the same way, so
T7's own verification ran the identical commands against a scratch build directory instead (same
flags, different `-B`/`--prefix` path; see the task report for that literal transcript). The
`examples/viewer` commands further down were verified against these exact literal paths, no
substitution needed.

### Build the library, CLI, and tests

```sh
cmake -B qt/build -S qt -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build qt/build
ctest --test-dir qt/build --output-on-failure
```

### Render a DSL program to a PNG

```sh
qt/build/nm-render --dsl parity/corpus/weird_mandala.dsl --size 512x512 --time 0.5 --frames 1 --out /tmp/out.png
```

### Build and run the live viewer example

With no installed `noisemaker-qt` package on `CMAKE_PREFIX_PATH`, this builds against the sibling
`qt/` tree directly (`add_subdirectory`):

```sh
cmake -B examples/viewer/build -S examples/viewer -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build examples/viewer/build
examples/viewer/build/viewer              # opens a window with a live animated render
examples/viewer/build/viewer --selfcheck  # headless-friendly: renders ~3s, screenshots to
                                           # /tmp/viewer.png, asserts the result is non-flat and
                                           # that no GL error was observed, exits 0 (pass) / 1 (fail)
```

### Build the viewer against an *installed* package instead

`libnoisemaker-qt` is `find_package`-consumable: installing it exports a static lib, its public
headers, and its runtime shader/effect data (loaded from disk at runtime — never compiled in) under
one prefix.

```sh
cmake --install qt/build --prefix /path/to/some/prefix
cmake -B examples/viewer/build2 -S examples/viewer -G Ninja \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/path/to/some/prefix"
cmake --build examples/viewer/build2
examples/viewer/build2/viewer --selfcheck
```

## Status, parity, and coverage

**210 effect definitions, 311/311 shaders byte-identical, 6/6 compiler oracle gates at 345/345,
and a corpus-wide pixel sweep at 289 PASS / 45 NEAR / 1 CHAOS / 0 FAIL of 335** — crystallized
from a from-scratch rebuild and full re-verification. Full detail (the coverage table by
namespace, every NEAR mechanism with its evidence, the CHAOS entry's isolation evidence, timed and
live-DSL results, and known limits) is tracked in **[STATUS.md](STATUS.md)**, the single source of
truth; this README does not duplicate it further.

## Architecture

See **[ARCHITECTURE.md](ARCHITECTURE.md)** for the render-graph seam, the OpenGL-vs-RHI decision
record, the shader corpus policy, the runtime model, and the compiler design, and
**[PORTING-GUIDE.md](PORTING-GUIDE.md)** for the porting rules (shader byte-copy policy, GL state
parity rules, compiler porting rules) anyone touching this port needs to follow.

## License and trademark

Code is MIT-licensed — see [LICENSE](LICENSE). "Noisemaker" and "Noise Factor" naming is governed
by [TRADEMARK.md](TRADEMARK.md); read it before using either name for a derivative product.
