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
- **`libnoisemaker-qt-quick`** (CMake target `noisemaker-qt::quick`, `qt/quick/`) — optional:
  `nm::NoisemakerItem`, a Qt Quick item that compiles a DSL program and shows its live render in a
  QML scene. Built when Qt Quick is installed (`NM_QT_BUILD_QUICK`).
- **`nm-render`** (`qt/tools/nm-render/`) — an offscreen render CLI: `--dsl <file>` (live compiler)
  or `--graph <json>` (a pre-exported graph) in, a PNG out. Drives the parity harness and doubles
  as a standalone batch renderer. `nm-render --help` lists every mode and flag. It reads its data
  from `NOISEMAKER_QT_DATA_ROOT` when that is set, else from `share/noisemaker-qt/noisemaker` beside
  its `bin/` directory (an install layout), else from the source tree's `qt/noisemaker`.
- **`examples/quick`** — `NoisemakerItem` filling a `QQuickView`, with compiler errors shown over
  the render and Space to pause.
- **`examples/viewer`** — a minimal live `QOpenGLWidget` embedding: DSL in (the live compiler path,
  `nm::compileGraph`), an animated render out, at roughly 60fps. It follows the window size with
  `nm::Backend::resize()` and presents each frame with a GPU blit of
  `nm::Backend::renderSurfaceTexture()`. Widgets/OpenGLWidgets are a dependency of this example
  only, never of `libnoisemaker-qt` itself.

## Requirements

The runtime needs an OpenGL 4.1 core profile context; shaders compile as GLSL 330 core. Qt 6.9 or later (Core, Gui and OpenGL; Quick for the optional item) is required, with CMake 3.21 or later and a C++17 compiler. Rendered parity is verified on macOS (Apple Silicon) over 353 fixtures. CI builds and runs the unit tests on Linux, Windows and macOS, and grades a smoke set of rendered fixtures on Linux and Windows with Mesa llvmpipe. Open platform limits are in [completion gaps](docs/COMPLETION_GAPS.md).

Qt 6.9 is the first version that can set a variable font's weight axis, which `text()` needs for the bundled Nunito. Measured on 2026-09-24: Qt 6.9.3 on Linux arm64 (gcc 13.3, Mesa llvmpipe) passes all 22 tests. Qt 6.4.2, the Ubuntu 24.04 package, builds and passes all tests except the text weight checks: it draws Nunito at its default ExtraLight weight for every program. CMake warns when it finds a Qt older than 6.9.

## Quickstart

Run the commands from the repository root. Each was run as written on macOS 26.5 with Homebrew Qt 6.11.1 and CMake 4.4 on 2026-09-24. Replace `/opt/homebrew/opt/qt` with your Qt installation (for example `~/Qt/6.11.1/macos`, `~/Qt/6.11.1/gcc_64` or `C:\Qt\6.11.1\msvc2022_64`). Any CMake generator works; add `-G Ninja` if you have Ninja. With a multi-configuration generator (Visual Studio, Xcode), add `--config Release` to the build commands and find the programs under `Release/`.

### Build the library, CLI, and tests

```sh
cmake -B qt/build -S qt -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build qt/build --parallel
ctest --test-dir qt/build --output-on-failure
```

### Render a DSL program to a PNG

```sh
qt/build/nm-render --dsl parity/corpus/weird_mandala.dsl --size 512x512 --time 0.5 --frames 1 --out qt/build/out.png
```

### Build and run the live viewer example

With no installed `noisemaker-qt` package on `CMAKE_PREFIX_PATH`, this builds against the sibling
`qt/` tree directly (`add_subdirectory`):

```sh
cmake -B examples/viewer/build -S examples/viewer -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build examples/viewer/build --parallel
examples/viewer/build/viewer              # opens a resizable window with a live animated render
examples/viewer/build/viewer --selfcheck  # renders 30 frames, resizes the window to 640x360 and
                                           # renders 30 more; each grab must match the window's
                                           # pixel size, be non-flat and show no GL error. Saves
                                           # viewer.png in the temp directory (or a path given
                                           # after --selfcheck); exits 0 (pass) / 1 (fail)
```

### Build the viewer against an *installed* package instead

`libnoisemaker-qt` supports `find_package`. Installation puts a static library, its public headers, the package config with its version file, the runtime data (`share/noisemaker-qt/noisemaker/`: shaders, effect definitions, the built-in meshes and the bundled font), the `nm-render` CLI (`bin/nm-render`, which finds that data from any working directory), and the licenses (`share/doc/noisemaker-qt/`) under one prefix. The runtime loads the data from disk; it is never compiled into the library.

```sh
cmake --install qt/build --prefix /path/to/some/prefix
cmake -B examples/viewer/build2 -S examples/viewer -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/homebrew/opt/qt;/path/to/some/prefix"
cmake --build examples/viewer/build2 --parallel
examples/viewer/build2/viewer --selfcheck
```

A consumer asks for the release series it was written against. The package is `0.x`, so a new minor version may change the API; `find_package` accepts only the same minor version:

```cmake
find_package(noisemaker-qt 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE noisemaker-qt::noisemaker-qt)
target_compile_definitions(my_app PRIVATE NM_DATA_ROOT="${NOISEMAKER_QT_DATA_ROOT}")
```

Pass `NOISEMAKER_QT_DATA_ROOT` (the installed `share/noisemaker-qt/noisemaker`) to `nm::EffectRegistry::loadAll()` and `nm::Backend::setup()`. Moving the prefix moves the data root with it: the package config derives every path from its own location.

To uninstall, remove the files that `cmake --install` recorded in `qt/build/install_manifest.txt` (its last line has no newline), then the package's directories, which are empty after that:

```sh
while IFS= read -r file || [ -n "$file" ]; do rm -f "$file"; done < qt/build/install_manifest.txt
rm -r /path/to/some/prefix/include/noisemaker-qt /path/to/some/prefix/lib/cmake/noisemaker-qt \
  /path/to/some/prefix/share/noisemaker-qt /path/to/some/prefix/share/doc/noisemaker-qt
```

In PowerShell on Windows:

```powershell
Get-Content qt/build/install_manifest.txt | Where-Object { $_ } | ForEach-Object { Remove-Item -LiteralPath $_ }
Remove-Item -Recurse C:/path/to/prefix/include/noisemaker-qt, C:/path/to/prefix/lib/cmake/noisemaker-qt, `
  C:/path/to/prefix/share/noisemaker-qt, C:/path/to/prefix/share/doc/noisemaker-qt
```

### Show a program in Qt Quick

`nm::NoisemakerItem` (`qt/quick/noisemaker_item.h`) is a `QQuickFramebufferObject`. It needs the OpenGL scene graph backend with a 4.1 core profile context, so set both before creating the application, then register the QML type:

```cpp
QSurfaceFormat format;
format.setVersion(4, 1);
format.setProfile(QSurfaceFormat::CoreProfile);
QSurfaceFormat::setDefaultFormat(format);
QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
QGuiApplication app(argc, argv);
nm::registerQmlTypes(); // import Noisemaker 1.0
```

```qml
import Noisemaker 1.0

NoisemakerItem {
    anchors.fill: parent
    dataRoot: noisemakerDataRoot // NOISEMAKER_QT_DATA_ROOT, passed in from C++
    program: "search synth\nnoise(seed: 3).write(o0)\nrender(o0)"
    onErrorStringChanged: if (errorString) console.warn(errorString)
}
```

The item renders at its size in device pixels, every frame while `running` is true, and at `time` (normalized loop time) while it is false. A size change or a new `program` or `dataRoot` starts the program over. `filter/text` steps are drawn. A compile or setup error sets `errorString`, and the item shows transparent pixels until the program works again. Link it with:

```cmake
find_package(noisemaker-qt 0.1 CONFIG REQUIRED COMPONENTS Quick)
target_link_libraries(my_app PRIVATE noisemaker-qt::quick)
```

The example builds against an installed package or the `qt/` tree, like the viewer:

```sh
cmake -B examples/quick/build -S examples/quick -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build examples/quick/build --parallel
examples/quick/build/quick              # a resizable window; Space pauses
examples/quick/build/quick --selfcheck  # renders 30 frames, resizes to 640x360, breaks the program and
                                         # restores it; saves quick.png in the temp directory; exits 0 / 1
```

### Embed the library in another CMake project

A host project can add `qt/` with `add_subdirectory` or `FetchContent`. Then only the library builds. `nm-render`, the tests, the Qt Quick item and the install rules default ON only for a top-level build. Override them with `NM_QT_BUILD_TOOLS`, `NM_QT_BUILD_TESTS`, `NM_QT_BUILD_QUICK` and `NM_QT_INSTALL`.

```cmake
find_package(Qt6 COMPONENTS Core Gui OpenGL REQUIRED)
add_subdirectory(path/to/noisemaker-for-qt/qt ${CMAKE_BINARY_DIR}/noisemaker-qt)
target_link_libraries(my_host PRIVATE noisemaker-qt::noisemaker-qt)
target_compile_definitions(my_host PRIVATE NM_DATA_ROOT="${NOISEMAKER_QT_DATA_ROOT}")
```

`noisemaker-qt::noisemaker-qt` and `NOISEMAKER_QT_DATA_ROOT` have the same names in the embedded and the installed forms. `NOISEMAKER_QT_DATA_DIR` is an alias, and the target also carries a `NOISEMAKER_QT_DATA_ROOT` property. Pass this directory explicitly to `nm::EffectRegistry::loadAll()` and `nm::Backend::setup()`; `loadAll()` throws, naming the directory, when it holds no effect definitions. `nm::EffectRegistry::defaultDataRoot()` reads the `NOISEMAKER_QT_DATA_ROOT` environment variable, else the working-directory-relative `qt/noisemaker`.

### Supply host inputs at run time

`nm::Backend` (`qt/noisemaker/runtime/backend.h`) mirrors the reference host APIs. When the host passes its own context to `setup()`, that context must be current for each call, as for `render()`. A Backend created with `setup(nullptr, ...)` makes its own context current.

- External textures: `synth/media` reads `imageTex_step_<N>` and `filter/text` reads `textTex_step_<N>`. `Backend::externalTextureIds(graph)` lists them. Upload pixels with `updateTextureFromSource(texId, image, {flipY})`, or bind a host GL texture with `setExternalTexture(texId, glTexture, size)`. The reference demo uploads media with `flipY = false` and text with `flipY = true`. Unsupplied ids sample a transparent-black default. For text, `nm::updateTextTextures(backend, graph, dataRoot, size)` (`qt/noisemaker/runtime/text_texture.h`) draws each `filter/text` step's parameters the way the reference canvas host does and uploads the result. It uses the bundled Nunito font for the default family. `parity/check_text_canvas.mjs` compares it with a Chromium canvas.
- Live parameters: `applyStepParameterValues(graph, registry, {"step_N": {param: value}})` and `setUniform(graph, name, value)` change uniforms without a recompile. For media, set `imageSize` to the uploaded size. Feedback surfaces persist, because they are keyed by texture id. A recompiled graph with the same structure also keeps them.
- Size and presentation: `resize(size)` changes the render size, as the reference `Pipeline.resize` does: every surface starts over at the new size. Upload text again after a resize. `renderSurfaceTexture()` returns the GL texture of the presented frame (bottom-up rows) for drawing on the GPU; `readSurface()` reads it back to a top-down `QImage`. `releaseGl()` frees the Backend's GL objects while a host context is still current.
- CPU overlays: `filter/fibers`, `filter/scratches` and `filter/strayHair` draw an overlay on the CPU when their seed, density or the render size changes. A 1920x1080 trace takes seconds. By default `render()` waits for it, so every frame shows the completed overlay; `nm-render`, the export kit and the goldens use this mode. A live host calls `setOverlayTraceMode(nm::OverlayTraceMode::Background)` after `setup()`. Then the trace runs on a worker thread and `render()` does not wait. The node keeps its previous overlay until the next `render()` after the trace completes; before the first trace at the current size completes, the overlay is transparent. The completed overlay is byte-identical to the synchronous one, and a newer change replaces a running trace. A host that renders only on demand renders again while `overlayTracesPending()` is true. `waitForOverlayTraces()` blocks until the traces finish, for a settled still. The viewer example and `nm::NoisemakerItem` select Background mode.
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

## Versions and releases

The port has one version series, `0.1`. It is the CMake project version (`find_package(noisemaker-qt 0.1)`) and the `series` in `export-kit/kit.config.json`; configure fails if they differ. In `0.x`, a new minor version may change the API.

A push to `main` that changes `qt/noisemaker/`, `export-kit/` or `LICENSE` releases the export kit. `.github/workflows/export-kit.yml` dispatches the Noise Factor release workflow, which builds the kit from that commit, validates it, publishes it at `https://kits.noisedeck.app/qt/<version>/`, points `qt/0.1/` and `qt/0/` at it, and tags the commit `kit-qt-v0.1.<n>`. A published version directory never changes. Noisedeck's "Export shader pipeline" reads `qt/0/`, so a new export uses the newest `0.x` kit. The kit's `kit.json` records its version and source commit, and an export's `noisedeck-export.json` records that commit as `kitSha`.

A C++ project pins the source of a kit release by its tag:

```cmake
include(FetchContent)
FetchContent_Declare(noisemaker_qt
    GIT_REPOSITORY https://github.com/noisefactorllc/noisemaker-for-qt.git
    GIT_TAG kit-qt-v0.1.29
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR qt)
FetchContent_MakeAvailable(noisemaker_qt)
target_link_libraries(my_app PRIVATE noisemaker-qt::noisemaker-qt)
target_compile_definitions(my_app PRIVATE NM_DATA_ROOT="${NOISEMAKER_QT_DATA_ROOT}")
```

Set `NM_QT_BUILD_QUICK` to `ON` before `FetchContent_MakeAvailable` to build `noisemaker-qt::quick` too. There are no separate release notes: `git log --oneline kit-qt-v0.1.<m>..kit-qt-v0.1.<n>` lists the changes between two releases.

## License and trademark

Code is MIT-licensed. See [LICENSE](LICENSE). The shaders and built-in meshes under `qt/noisemaker/` are unmodified copies of the reference engine's files ([noisefactorllc/noisemaker](https://github.com/noisefactorllc/noisemaker)), and the effect definitions are generated from its definitions (`tools/convert-definitions.mjs`); the reference is MIT-licensed by the same copyright holder. Shader code adapted from third-party work keeps its license notice in the shader source; the reference's [CREDITS.md](https://github.com/noisefactorllc/noisemaker/blob/main/CREDITS.md) lists those works.

The compiler's JavaScript syntax check (`qt/noisemaker/compiler/js_syntax.cpp` and `js_regexp.cpp`) is a C++ port of acorn 8.16, MIT-licensed, Copyright (C) 2012-2022 by various contributors. Its notice is in those files and in [acorn-LICENSE.txt](qt/noisemaker/compiler/acorn-LICENSE.txt), which the install and the export kit also carry.

The MIT license does not cover the bundled font:

- `qt/noisemaker/fonts/Nunito/Nunito-VariableFont_wght.ttf` is Nunito Version 3.602, Copyright 2014 The Nunito Project Authors ([googlefonts/nunito](https://github.com/googlefonts/nunito)), licensed under the SIL Open Font License 1.1 ([OFL.txt](qt/noisemaker/fonts/Nunito/OFL.txt)). It is an unmodified copy of the reference's `demo/font/Nunito/Nunito-VariableFont_wght.ttf` (added in `noisefactorllc/noisemaker` commit `9f23756d`, identical at `c9ee8a04`). SHA-256: `707f6b338cfd21e95f05a88169ef7647d01ad8da76623846c092f3118f762a08`.

[TRADEMARK.md](TRADEMARK.md) governs use of the "Noisemaker" and "Noise Factor" names. Read it before using either name for a derivative product.
