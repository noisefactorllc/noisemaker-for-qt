# {{NM_PROGRAM_NAME}} — Qt 6

This is a C++17/CMake project using [Noisemaker for Qt](https://github.com/noisefactorllc/noisemaker-for-qt). It compiles `program.dsl` in-process and renders it to a PNG through Qt's `QOpenGL` backend. It fetches nothing at run time.

## Build

Install Qt 6.9 or later (Core, Gui and OpenGL), CMake 3.21 or later, and a C++17 compiler. Older Qt 6 versions build it, but draw `text()` at the font's default weight. The renderer needs an OpenGL 4.1 core profile context. Point `CMAKE_PREFIX_PATH` at your Qt installation when CMake cannot find it automatically.

```sh
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/<platform>
cmake --build build --config Release
```

Any CMake generator works. Single-configuration generators build `Release` unless you set `CMAKE_BUILD_TYPE`. With a multi-configuration generator (Visual Studio, Xcode), the program is under `build/Release/`.

## Render

```sh
./build/noisemaker-qt-export
```

That reads `program.dsl` from this project and writes `output.png` in the current directory, at 1024×1024. You can also pass another DSL file and output path, and options:

```sh
./build/noisemaker-qt-export path/to/program.dsl path/to/output.png --size 1920x1080
```

| Option | Effect |
| --- | --- |
| `--size WxH` | Output size in pixels. Default `1024x1024`. |
| `--time T` | Loop time of the output frame, `0 <= T < 1` of a 10 second loop. Default `0.5`. |
| `--frames N` | Frames rendered 1/60 s apart, ending at `--time`. Default `8`. Feedback and simulation effects start empty, so they need more frames: `--frames 600` is ten seconds. |
| `--media FILE` | Image for every `media()` step: PNG, JPEG, BMP, GIF or any format your Qt build reads. |
| `--audio FILE` | WAV file for `scope()`, `spectrum()` and `audio()`: integer PCM of 8, 16, 24 or 32 bits, or 32 or 64-bit float. |
| `--midi FILE` | Standard MIDI File for `roll()` and `midi()`. |
| `--mesh FILE` | OBJ file for `meshLoader()`. |
| `--help` | Print the options. |

The program exits with status 0 after writing the PNG, 1 when the program, an input file or rendering fails, and 2 for a command-line error. Messages go to standard error.

### External inputs

Noisedeck exports the program, not the media or the live devices it ran with. Supply them here:

- **Media.** A `media()` step shows the `--media` image, uploaded as Noisedeck uploads it. Without `--media` it shows nothing, and the renderer says so on standard error.
- **Text.** `text()` steps draw their text on the CPU the way Noisedeck's text canvas does, with the font, size, position, rotation, colour, alignment and style in the program. The default font, Nunito, ships in `fonts/`. Generic families (`serif`, `sans-serif`, `monospace`, `cursive`, `fantasy`) and other font names resolve through your system's fonts, so they can differ from the font your browser picked. Glyph edges differ slightly from a browser canvas.
- **Audio.** `--audio` plays the WAV from the start of the loop, so the output frame at `--time 0.5` hears the file at 5 seconds. Without `--audio`, a program that uses audio hears silence: `scope()` draws a flat line through the middle and `spectrum()` draws only its baseline.
- **MIDI.** `--midi` plays the file's notes, controllers and pitch bends from the start of the loop. Without it no notes are held. `roll()` scrolls with time, so render enough `--frames` to see its history.
- **Meshes.** A `meshLoader()` step starts with its built-in mesh (a sphere), as in the Noisemaker demo. `--mesh` loads an OBJ file instead. The built-in meshes ship in `share/meshes/`.

## Effects

{{NM_EFFECT_LIST}}

## What this export cannot do

- Video files, cameras, live microphones and live MIDI ports. The renderer takes still images, WAV files and MIDI files.
- It writes one PNG per run. For an animation, run it once per frame with increasing `--time`.

## What's inside

| Path | What it is |
| --- | --- |
| `program.dsl` | Your program's source, exactly as Noisedeck had it. |
| `noisedeck-export.json` | What was exported, when, and from which port commit (`kitSha`). |
| `CMakeLists.txt`, `src/` | The renderer: command line, input files and the render loop. |
| `engine/` | The Noisemaker for Qt compiler and runtime. Present if you kept **include engine code** checked. |
| `effects/`, `fonts/`, `share/` | Effect definitions, the bundled Nunito font and the built-in meshes, read at run time. Present with the engine code. |
| `shaders/` | The desktop GLSL `.frag` and `.vert` stages the engine loads at run time. Present if you kept **include shader code** checked. |
| `LICENSES/` | Licenses for everything shipped here. |

The export includes no WebGPU, Unity, Godot or TouchDesigner shaders. The renderer reads `effects/`, `fonts/`, `share/` and `shaders/` from this folder at run time, so keep them beside `CMakeLists.txt`.

## Export options

The default export includes both the pinned `noisemaker-for-qt` C++ engine and effect catalog and its matching shader corpus. If you omitted either source-code option in Noisedeck, copy the same files from a checkout of the Qt port at the commit that `kitSha` in `noisedeck-export.json` names: `qt/noisemaker/compiler/` and `qt/noisemaker/runtime/` to `engine/noisemaker/`, and `qt/noisemaker/effects/`, `qt/noisemaker/fonts/`, `qt/noisemaker/share/` and `qt/noisemaker/shaders/` to `effects/`, `fonts/`, `share/` and `shaders/`.

## License

The Noisemaker engine and the Qt port are MIT licensed: `LICENSES/noisemaker-MIT.txt` and `LICENSES/noisemaker-for-qt-LICENSE.txt`. The Nunito font in `fonts/Nunito/` is licensed under the SIL Open Font License 1.1: `LICENSES/Nunito-OFL.txt`. Your program and the imagery it renders are yours.
