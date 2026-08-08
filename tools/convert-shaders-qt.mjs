#!/usr/bin/env node
// convert-shaders-qt.mjs — AUTOMATED GLSL shader-corpus mirror.
//
// Walks NM_REFERENCE_ROOT/shaders/effects/<ns>/<func>/glsl/*.glsl and copies each file BYTE FOR
// BYTE to qt/noisemaker/shaders/effects/<ns>/<func>/<basename>.frag.
//
// PORTING-GUIDE.md rule 1: shaders are byte-copies, never ports. This tool skips nothing and
// transforms nothing — dialect handling (#version header, packHalf2x16 polyfill) happens at
// LOAD TIME in the Qt renderer, not here. The `.glsl` -> `.frag` rename is cosmetic (matches
// the destination GL shader stage); file contents are untouched.
//
// Enumeration is a plain directory walk (readdirSync + isDirectory), NOT gated on the presence
// of a sibling definition.js the way convert-definitions.mjs's effect enumeration is. That is
// deliberate: shaders/effects/filter/_shared/glsl/*.glsl has no definition.js (it is a shared
// GLSL helper, not an effect) but still needs to land in the corpus for byte-parity, and a
// definition.js-gated walk would silently skip it.
//
// Scope note: a handful of effect dirs (wormhole, flow3d, dla, lenia, physarum, meshRender,
// pointsBillboardRender, pointsRender) carry a `deposit.vert`/`deposit.frag` (or
// `render.vert`/`render.frag`, `depositGrid.vert`/`depositGrid.frag`) pair directly inside their
// glsl/ directory, already named for their GL shader stage. Those are real vertex+fragment
// PROGRAM pairs for the point/mesh rendering pipeline (PORTING-GUIDE.md's GL state parity rules
// table: GL_PROGRAM_POINT_SIZE, deposit blend mode) — a different loading mechanism than the
// full-screen-quad fragment-only effect programs this tool mirrors, and out of this task's scope
// (295 = the `*.glsl` count only, matching the brief exactly). They are left untouched here.
//
// Usage:
//   node convert-shaders-qt.mjs             # copy all programs
//   node convert-shaders-qt.mjs --dry-run   # print summary, write nothing
//
// Env:
//   NM_REFERENCE_ROOT  path to the Noisemaker reference repo (REQUIRED; no default, no sibling assumed)
//   NM_OUT_DIR         override output dir (default: the repo qt/noisemaker/shaders/effects)

import { readdirSync, statSync, mkdirSync, copyFileSync } from 'node:fs'
import { join, dirname, resolve, basename } from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = dirname(fileURLToPath(import.meta.url))
// Reference engine path comes from NM_REFERENCE_ROOT (a checkout of the Noisemaker reference
// repo); this repo does not assume a sibling checkout.
if (!process.env.NM_REFERENCE_ROOT) {
  throw new Error('NM_REFERENCE_ROOT is not set. Point it at a checkout of the Noisemaker\n'
    + 'reference repo (the JS/WebGL2 engine), e.g. NM_REFERENCE_ROOT=/path/to/noisemaker.\n'
    + 'This repo does NOT assume a sibling checkout.')
}
const REFERENCE_ROOT = resolve(process.env.NM_REFERENCE_ROOT)
const EFFECTS_DIR = join(REFERENCE_ROOT, 'shaders', 'effects')
const OUT_DIR = process.env.NM_OUT_DIR
  ? resolve(process.env.NM_OUT_DIR)
  : resolve(__dirname, '..', 'qt', 'noisemaker', 'shaders', 'effects')

function isDir (p) {
  try { return statSync(p).isDirectory() } catch { return false }
}

// Enumerate every GLSL program: shaders/effects/<ns>/<func>/glsl/*.glsl for every directory
// entry under shaders/effects (namespaces auto-discovered, not hardcoded — mirrors the brief's
// literal `shaders/effects/*/*/glsl/*.glsl` wildcard walk). This naturally picks up
// filter/_shared alongside the per-effect directories: _shared is just another `<func>`-shaped
// entry under `filter` with its own glsl/ subdirectory.
function* enumeratePrograms () {
  for (const ns of readdirSync(EFFECTS_DIR).sort()) {
    const nsDir = join(EFFECTS_DIR, ns)
    if (!isDir(nsDir)) continue // skip manifest.json, strings.*.json, HELP_TEMPLATE.md
    for (const func of readdirSync(nsDir).sort()) {
      const funcDir = join(nsDir, func)
      if (!isDir(funcDir)) continue
      const glslDir = join(funcDir, 'glsl')
      if (!isDir(glslDir)) continue
      for (const file of readdirSync(glslDir).sort()) {
        if (!file.endsWith('.glsl')) continue // skip nothing that matches; transform nothing that does
        yield { ns, func, srcPath: join(glslDir, file), base: basename(file, '.glsl') }
      }
    }
  }
}

function main () {
  const argv = process.argv.slice(2)
  const dryRun = argv.includes('--dry-run')

  let copied = 0
  let shared = 0
  for (const { ns, func, srcPath, base } of enumeratePrograms()) {
    const outDir = join(OUT_DIR, ns, func)
    const outPath = join(outDir, `${base}.frag`)
    if (!dryRun) {
      mkdirSync(outDir, { recursive: true })
      copyFileSync(srcPath, outPath)
    }
    copied++
    if (func === '_shared') shared++
    process.stderr.write(`[convert-shaders] ${ns}/${func}/glsl/${base}.glsl -> shaders/effects/${ns}/${func}/${base}.frag${dryRun ? ' (dry-run)' : ''}\n`)
  }

  process.stderr.write(`\n[convert-shaders] ${copied - shared} effect program(s) + ${shared} shared helper(s) = ${copied} total${dryRun ? ' (dry-run, nothing written)' : ''}\n`)
  console.log(`COPIED ${copied} programs`)
}

if (basename(process.argv[1] || '') === 'convert-shaders-qt.mjs') {
  main()
}
