#!/usr/bin/env node
// convert-shaders-qt.mjs — AUTOMATED GLSL shader-corpus mirror.
//
// Walks NM_REFERENCE_ROOT/shaders/effects/<ns>/<func>/glsl/* and copies each file BYTE FOR BYTE
// into qt/noisemaker/shaders/effects/<ns>/<func>/. Two file shapes live in glsl/ directories:
//
//   *.glsl          full-screen-quad fragment-only effect programs -> renamed to <basename>.frag
//   *.vert / *.frag  ALREADY named for their GL shader stage (vertex+fragment PROGRAM pairs for
//                    the point/mesh rendering pipeline's deposit/render passes) -> copied verbatim,
//                    extension UNCHANGED. Fix round 1 (reviewer finding): the first pass of this
//                    tool only matched `*.glsl` and silently dropped these 16 files across 8 effect
//                    dirs (wormhole, flow3d, dla, lenia, physarum, meshRender,
//                    pointsBillboardRender, pointsRender) — confirmed zero basename collisions
//                    between the .glsl-derived `.frag` outputs and these native `.vert`/`.frag`
//                    files anywhere in the reference tree, so both shapes coexist safely per
//                    output directory.
//
// PORTING-GUIDE.md rule 1: shaders are byte-copies, never ports. This tool skips nothing that
// matches a known glsl/-directory file shape and transforms nothing byte-wise — dialect handling
// (#version header, packHalf2x16 polyfill) happens at LOAD TIME in the Qt renderer, not here.
//
// Enumeration is a plain directory walk (readdirSync + isDirectory), NOT gated on the presence
// of a sibling definition.js the way convert-definitions.mjs's effect enumeration is. That is
// deliberate: shaders/effects/filter/_shared/glsl/*.glsl has no definition.js (it is a shared
// GLSL helper, not an effect) but still needs to land in the corpus for byte-parity, and a
// definition.js-gated walk would silently skip it.
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

// Enumerate every program file under shaders/effects/<ns>/<func>/glsl/ for every directory entry
// under shaders/effects (namespaces auto-discovered, not hardcoded — mirrors the brief's literal
// `shaders/effects/*/*/glsl/*` wildcard walk). This naturally picks up filter/_shared alongside
// the per-effect directories: _shared is just another `<func>`-shaped entry under `filter` with
// its own glsl/ subdirectory.
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
        if (file.endsWith('.glsl')) {
          yield { ns, func, srcPath: join(glslDir, file), outName: `${basename(file, '.glsl')}.frag`, kind: 'glsl' }
        } else if (file.endsWith('.vert') || file.endsWith('.frag')) {
          // Already GL-stage-named in the reference; copy as-is, no rename.
          yield { ns, func, srcPath: join(glslDir, file), outName: file, kind: 'staged' }
        }
        // Anything else in a glsl/ dir is unexpected — skip (the reference tree today has no
        // other extensions here; this is not a silent-drop of a recognized shape).
      }
    }
  }
}

function main () {
  const argv = process.argv.slice(2)
  const dryRun = argv.includes('--dry-run')

  let copied = 0
  let shared = 0
  let staged = 0
  for (const { ns, func, srcPath, outName, kind } of enumeratePrograms()) {
    const outDir = join(OUT_DIR, ns, func)
    const outPath = join(outDir, outName)
    if (!dryRun) {
      mkdirSync(outDir, { recursive: true })
      copyFileSync(srcPath, outPath)
    }
    copied++
    if (func === '_shared') shared++
    if (kind === 'staged') staged++
    process.stderr.write(`[convert-shaders] ${ns}/${func}/glsl/${basename(srcPath)} -> shaders/effects/${ns}/${func}/${outName}${dryRun ? ' (dry-run)' : ''}\n`)
  }

  const glslDerived = copied - shared - staged
  process.stderr.write(`\n[convert-shaders] ${glslDerived} effect program(s) (.glsl) + ${staged} stage-named pair file(s) (.vert/.frag) + ${shared} shared helper(s) = ${copied} total${dryRun ? ' (dry-run, nothing written)' : ''}\n`)
  console.log(`COPIED ${copied} programs`)
}

if (basename(process.argv[1] || '') === 'convert-shaders-qt.mjs') {
  main()
}
