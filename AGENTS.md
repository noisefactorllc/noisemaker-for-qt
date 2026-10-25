# Noisemaker for Qt

Desktop OpenGL runtime (4.1 core context, GLSL 330 core shaders) and compiler for the Noisemaker shader platform.

## Rules

Never create, introduce or use symbolic links anywhere in checkouts, repositories, configuration, scripts or documentation. Every file is a regular file.

Write and present research documents in the established technical whitepaper style. Never use slop headlines, promotional or slogan headers, parenthetical subtitles in titles, stat cards, metric cards, decorative callouts, marketing-speak or invented report layouts.

## Testing & Build

- **C++ Unit tests**: `ctest --test-dir qt/build --output-on-failure` (or build target `test`)
- **Compiler Parity Gates**: `for f in parity/check_{shaders,definitions,registry,lex,parse,validate,expand,graph}.mjs; do NM_REFERENCE_ROOT=../noisemaker node "$f"; done`
- **Python Parity tests**: `pytest parity/`
- **Rebuilding C++ tree**: `cmake --build qt/build`
