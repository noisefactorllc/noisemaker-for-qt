# Noisemaker for Qt

**Noisemaker** is a procedural visual engine: you write small text programs — chains of
effects — and it renders live, animated GPU textures. **Noisemaker for Qt** brings that same
DSL compiler and effects library to Qt 6, as an idiomatic C++ library (`libnoisemaker-qt`) plus
an offscreen render CLI (`nm-render`), executed via classic `QOpenGL*` with pixel-level parity
against the reference engine.

**Status: under construction.** This port is in early scaffolding — no runtime or compiler code
yet. See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and [PORTING-GUIDE.md](PORTING-GUIDE.md)
for the porting rules.
