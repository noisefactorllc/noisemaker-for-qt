<!-- repo-hero -->
<a href="https://noisemaker.app/"><img src="docs/hero.jpg" alt="Noisemaker for Qt" width="100%"></a>

<sub>Open source from <a href="https://noisefactor.io">Noise Factor</a> &middot; <a href="https://github.com/noisefactorllc">more projects</a></sub>

# Noisemaker for Qt

Current measured support: [compatibility report](docs/COMPATIBILITY.md).

Current qualification limits: [completion gaps](docs/COMPLETION_GAPS.md).

> This package supports the "Export Shader Pipeline" feature in Noisedeck.app. The feature runs shader compositions on other platforms. Noise Factor derives this package from the upstream Noisemaker Engine project and tests it for pixel-level parity.

**Noisemaker** is a procedural visual engine. Small text programs combine effects into chains, compile to a render graph, and run live as animated GPU textures. **Noisemaker for Qt** brings the same DSL compiler and effects library to Qt 6. It includes a C++17 library (`noisemaker-qt`, namespace `nm::`), an offscreen render CLI (`nm-render`), and a live `QOpenGLWidget` example. These use classic `QOpenGL*` with pixel-level parity against the reference engine.

## Family context

This port is one of several hand-ports of the same reference engine. Siblings target Unity (HLSL), Godot (GDShader), TouchDesigner (GLSL), and other platforms. They share the following infrastructure:

- The engine-agnostic `reference/01–10` re-implementer specs.
- The render-graph JSON contract (`docs/GRAPH-JSON-SCHEMA.md`).
- The parity-harness design (`parity/`).

Unlike the HLSL/GDShader ports, this port shares the reference's shader *language*. Desktop GLSL belongs to the same family as the reference's WebGL2 GLSL ES source. This port therefore commits byte-identical reference GLSL instead of deriving it again. See [ARCHITECTURE.md](ARCHITECTURE.md), "Shader corpus" and "Why classic OpenGL", for the effect on parity risk.

## What's here

- **`libnoisemaker-qt`** (CMake target `noisemaker-qt`) — the DSL compiler
  (`qt/noisemaker/compiler/`: lexer → parser → validator → expander → resources → orchestrator)
  and the render-graph runtime (`qt/noisemaker/runtime/`: a `QOpenGL*` executor, `nm::Backend`).
  Qt 6 Core/Gui/OpenGL only — no Widgets/QML dependency.
- **`nm-render`** (`qt/tools/nm-render/`) — an offscreen render CLI: `--dsl <file>` (live compiler)
  or `--graph <json>` (a pre-exported graph) in, a PNG out. Drives the parity harness and doubles
  as a standalone batch renderer.
- **`examples/viewer`** — a minimal live `QOpenGLWidget` embedding: DSL in (the live compiler path,
  `nm::compileGraph`), an animated render out, at roughly 60fps. It follows the window size with
  `nm::Backend::resize()` and presents each frame with a GPU blit of
  `nm::Backend::renderSurfaceTexture()`. Widgets/OpenGLWidgets are a dependency of this example
  only, never of `libnoisemaker-qt` itself.

## Requirements

The runtime needs an OpenGL 4.1 core profile context; shaders compile as GLSL 330 core. Qt 6 Core, Gui and OpenGL are required, with CMake 3.21 or later and a C++17 compiler. Rendered parity is verified on macOS (Apple Silicon). CI builds and runs the unit tests on Linux with Mesa llvmpipe. Windows is not yet verified; see [completion gaps](docs/COMPLETION_GAPS.md).

## Quickstart

Every command below was verified by literally running it before it was written down here.
The commands below use the core library's canonical build directory, `qt/build` (gitignored). Any out-of-tree build directory works the same way. T7's verification used identical commands with a scratch build directory: the same flags, but a different `-B`/`--prefix` path. See the task report for the literal transcript. The
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
examples/viewer/build/viewer              # opens a resizable window with a live animated render
examples/viewer/build/viewer --selfcheck  # renders 30 frames, resizes the window to 640x360 and
                                           # renders 30 more; each grab must match the window's
                                           # pixel size, be non-flat and show no GL error. Saves
                                           # viewer.png in the temp directory (or a path given
                                           # after --selfcheck); exits 0 (pass) / 1 (fail)
```

### Build the viewer against an *installed* package instead

`libnoisemaker-qt` supports `find_package`. Installation exports a static library, its public headers, and its runtime shader/effect data under one prefix. The runtime loads this data from disk. The data is never compiled into the library.

```sh
cmake --install qt/build --prefix /path/to/some/prefix
cmake -B examples/viewer/build2 -S examples/viewer -G Ninja \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/path/to/some/prefix"
cmake --build examples/viewer/build2
examples/viewer/build2/viewer --selfcheck
```

### Embed the library in another CMake project

A host project can add `qt/` with `add_subdirectory` or `FetchContent`. Then only the library builds. `nm-render`, the tests, and the install rules default ON only for a top-level build. Override them with `NM_QT_BUILD_TOOLS`, `NM_QT_BUILD_TESTS`, and `NM_QT_INSTALL`.

```cmake
find_package(Qt6 COMPONENTS Core Gui OpenGL REQUIRED)
add_subdirectory(path/to/noisemaker-for-qt/qt ${CMAKE_BINARY_DIR}/noisemaker-qt)
target_link_libraries(my_host PRIVATE noisemaker-qt::noisemaker-qt)
target_compile_definitions(my_host PRIVATE NM_DATA_ROOT="${NOISEMAKER_QT_DATA_ROOT}")
```

`noisemaker-qt::noisemaker-qt` and `NOISEMAKER_QT_DATA_ROOT` have the same names in the embedded and the installed forms. `NOISEMAKER_QT_DATA_DIR` is an alias, and the target also carries a `NOISEMAKER_QT_DATA_ROOT` property. Pass this directory explicitly to `nm::EffectRegistry::loadAll()` and `nm::Backend::setup()`. `nm::EffectRegistry::defaultDataRoot()` reads the `NOISEMAKER_QT_DATA_ROOT` environment variable, else the working-directory-relative `qt/noisemaker`.

### Supply host inputs at run time

`nm::Backend` (`qt/noisemaker/runtime/backend.h`) mirrors the reference host APIs. When the host passes its own context to `setup()`, that context must be current for each call, as for `render()`. A Backend created with `setup(nullptr, ...)` makes its own context current.

- External textures: `synth/media` reads `imageTex_step_<N>` and `filter/text` reads `textTex_step_<N>`. `Backend::externalTextureIds(graph)` lists them. Upload pixels with `updateTextureFromSource(texId, image, {flipY})`, or bind a host GL texture with `setExternalTexture(texId, glTexture, size)`. The reference demo uploads media with `flipY = false` and text with `flipY = true`. Unsupplied ids sample a transparent-black default. For text, `nm::updateTextTextures(backend, graph, dataRoot, size)` (`qt/noisemaker/runtime/text_texture.h`) draws each `filter/text` step's parameters the way the reference canvas host does and uploads the result. It uses the bundled Nunito font for the default family. `parity/check_text_canvas.mjs` compares it with a Chromium canvas.
- Live parameters: `applyStepParameterValues(graph, registry, {"step_N": {param: value}})` and `setUniform(graph, name, value)` change uniforms without a recompile. For media, set `imageSize` to the uploaded size. Feedback surfaces persist, because they are keyed by texture id. A recompiled graph with the same structure also keeps them.
- Size and presentation: `resize(size)` changes the render size, as the reference `Pipeline.resize` does: every surface starts over at the new size. Upload text again after a resize. `renderSurfaceTexture()` returns the GL texture of the presented frame (bottom-up rows) for drawing on the GPU; `readSurface()` reads it back to a top-down `QImage`. `releaseGl()` frees the Backend's GL objects while a host context is still current.
- Time: `render(graph, t)` takes normalized loop time. `Backend::normalizedLoopTime(elapsedSeconds, 10.0)` derives it. The Backend supplies `deltaTime` and `frame` like the reference, and `syncTime(t)` pauses without a time step.

## Status, parity, and coverage

Results measured on macOS (Apple Silicon) on 2026-09-24:

- **210 effect definitions.**
- **317/317 shaders byte-identical.**
- **Compiler oracle gates at 357/357** (lex, parse, validate, expand, graph), registry 5/5.
- **MIDI and audio state gates:** MIDI_STATE 66/66 and AUDIO_STATE 93/93 exact against the reference classes. AUDIO_ANALYZER 570/570 against Chromium's AnalyserNode.
- **Corpus-wide pixel sweep (2026-09-24): 298 PASS / 47 NEAR / 0 CHAOS / 2 FAIL of 347.** The failures are open gaps GAP-010 and GAP-011.

**[STATUS.md](STATUS.md)** is the single source of truth. It includes coverage by namespace, every NEAR mechanism with its evidence, and the CHAOS entry's isolation evidence. It also includes timed and live-DSL results and known limits. This README does not duplicate it further.

## Architecture

See **[ARCHITECTURE.md](ARCHITECTURE.md)** for the render-graph seam, OpenGL-vs-RHI decision record, shader corpus policy, runtime model, and compiler design. Follow **[PORTING-GUIDE.md](PORTING-GUIDE.md)** when changing this port. It covers the shader byte-copy policy, GL state parity rules, and compiler porting rules.

## License and trademark

Code is MIT-licensed. See [LICENSE](LICENSE). The MIT license does not cover the bundled font:

- `qt/noisemaker/fonts/Nunito/Nunito-VariableFont_wght.ttf` is Nunito Version 3.602, Copyright 2014 The Nunito Project Authors ([googlefonts/nunito](https://github.com/googlefonts/nunito)), licensed under the SIL Open Font License 1.1 ([OFL.txt](qt/noisemaker/fonts/Nunito/OFL.txt)). It is an unmodified copy of the reference's `demo/font/Nunito/Nunito-VariableFont_wght.ttf` (added in `noisefactorllc/noisemaker` commit `9f23756d`, identical at `c9ee8a04`). SHA-256: `707f6b338cfd21e95f05a88169ef7647d01ad8da76623846c092f3118f762a08`. [TRADEMARK.md](TRADEMARK.md) governs use of the "Noisemaker" and "Noise Factor" names. Read it before using either name for a derivative product.
