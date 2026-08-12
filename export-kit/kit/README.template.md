# {{NM_PROGRAM_NAME}} — Qt 6

This is a C++17/CMake project using [Noisemaker for Qt](https://github.com/noisefactorllc/noisemaker-for-qt). It compiles `program.dsl` in-process and renders a 1024×1024 PNG through Qt's `QOpenGL` backend.

## Build

Install Qt 6, CMake, Ninja, and a C++17 compiler. Point `CMAKE_PREFIX_PATH` at your Qt installation when CMake cannot find it automatically.

```sh
cmake -B build -S . -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x
cmake --build build
```

## Render

```sh
./build/noisemaker-qt-export
```

That reads `program.dsl` from this project and writes `output.png` in the current directory. You can also pass another DSL file and output path:

```sh
./build/noisemaker-qt-export path/to/program.dsl path/to/output.png
```

The generated project uses Qt 6 Core, Gui, and OpenGL. Its shaders are the Qt port's desktop GLSL `.frag` and `.vert` stages; no WebGPU, Unity, Godot, or TouchDesigner shaders are included.

## Effects

{{NM_EFFECT_LIST}}

## Export options

The default export includes both the pinned `noisemaker-for-qt` C++ engine/effect catalog and its matching shader corpus. If you omit either source-code option in Noisedeck, wire this skeleton to the same files from an existing copy of the Qt port before configuring it.
