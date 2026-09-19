# Noisemaker for Qt

Desktop OpenGL 3.3 runtime and compiler for the Noisemaker shader platform.

## Strict Rules

HARD, PERMANENT, INVIOLABLE BAN: BANNED FROM SYMLINKS. Never create, introduce, or use symbolic links anywhere in checkouts, repositories, configuration, scripts, or documentation. All files must be regular files. Zero exceptions.

HARD, PERMANENT, INVIOLABLE BAN: Research documents must be written and presented strictly in the established technical whitepaper style. Banned from slop headlines, promotional/slogan headers, parenthetical subtitles in titles, stat cards, metric cards, decorative callouts, marketing-speak, and invented report layouts. Zero exceptions.

## Testing & Build

- **C++ Unit tests**: `ctest --test-dir qt/build --output-on-failure` (or build target `test`)
- **Compiler Parity Gates**: `for f in parity/check_{shaders,definitions,registry,lex,parse,validate,expand,graph}.mjs; do NM_REFERENCE_ROOT=/Users/alex/platform/noisemaker node "$f"; done`
- **Python Parity tests**: `pytest parity/`
- **Rebuilding C++ tree**: `cmake --build qt/build`
