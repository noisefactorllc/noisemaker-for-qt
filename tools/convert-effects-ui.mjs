#!/usr/bin/env node
// convert-effects-ui.mjs — AUTOMATED effect UI-metadata regenerator.
//
// convert-definitions.mjs deliberately drops UI-only metadata (ui.*, step) because the
// renderer never reads it. Hosts that build parameter controls (sliders, dropdowns, toggles,
// category groups, help pages) do need it. This tool emits that metadata as a SIDECAR tree so
// the renderer-facing definitions under qt/noisemaker/effects stay byte-identical:
//
//   qt/noisemaker/effects-ui/<ns>/<func>.json
//     { name, namespace, func, [description], [tags],
//       params{ <key>: { [step], [ui] } },      // declaration order; only params with step or ui
//       [help] }                                 // help.md verbatim, when the effect has one
//   qt/noisemaker/effects-ui/strings.en.json     // shaders/effects/strings.en.json, byte copy
//
// `ui` is copied verbatim from the reference definition (label, control, category, hidden,
// enabledBy, hint, multiline, buttonLabel, format, ...), so new reference UI keys arrive without
// a tool change. Nothing here is consumed by nm::EffectRegistry or nm::Backend.
//
// Usage:
//   node convert-effects-ui.mjs                # regenerate the whole tree (replaces it)
//   node convert-effects-ui.mjs synth/noise    # regenerate one (ns/name)
//   node convert-effects-ui.mjs --dry-run      # print summary, write nothing
//
// Env:
//   NM_REFERENCE_ROOT  path to the Noisemaker reference repo (REQUIRED; no default, no sibling assumed)
//   NM_OUT_DIR         override output dir (default: the repo qt/noisemaker/effects-ui)

import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { basename, dirname, join, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const __dirname = dirname(fileURLToPath(import.meta.url))
if (!process.env.NM_REFERENCE_ROOT) {
  throw new Error('NM_REFERENCE_ROOT is not set. Point it at a checkout of the Noisemaker\n'
    + 'reference repo (the JS/WebGL2 engine), e.g. NM_REFERENCE_ROOT=/path/to/noisemaker.\n'
    + 'This repo does NOT assume a sibling checkout.')
}
const REFERENCE_ROOT = resolve(process.env.NM_REFERENCE_ROOT)
const EFFECTS_DIR = join(REFERENCE_ROOT, 'shaders', 'effects')
const OUT_DIR = process.env.NM_OUT_DIR
  ? resolve(process.env.NM_OUT_DIR)
  : resolve(__dirname, '..', 'qt', 'noisemaker', 'effects-ui')
const STRINGS_FILE = 'strings.en.json'

// Same namespace list as convert-definitions.mjs, so both trees cover the same effects.
const NAMESPACES = [
  'classicNoisedeck', 'filter', 'filter3d',
  'mixer', 'points', 'render', 'synth', 'synth3d'
]

function projectParams (globals) {
  const out = {}
  if (!globals) return out
  // Object.entries preserves declaration order, which hosts use as control order. DO NOT sort.
  for (const [key, spec] of Object.entries(globals)) {
    const p = {}
    if (spec.step !== undefined) p.step = spec.step
    if (spec.ui !== undefined) p.ui = spec.ui
    if (Object.keys(p).length > 0) out[key] = p
  }
  return out
}

function convertEffect (instance, namespace, name, effectDir) {
  const func = instance.func || name
  const def = {
    name: instance.name || func,
    namespace: instance.namespace || namespace,
    func
  }
  if (instance.description) def.description = instance.description
  if (instance.tags) def.tags = instance.tags
  def.params = projectParams(instance.globals)
  const helpPath = join(effectDir, 'help.md')
  if (existsSync(helpPath)) def.help = readFileSync(helpPath, 'utf8')
  return { func, def }
}

async function loadInstance (defPath) {
  const mod = await import(pathToFileURL(defPath).href)
  const d = mod.default
  return (typeof d === 'function') ? new d() : d
}

function * enumerateEffects (filter) {
  for (const namespace of NAMESPACES) {
    const nsDir = join(EFFECTS_DIR, namespace)
    if (!existsSync(nsDir)) continue
    for (const entry of readdirSync(nsDir).sort()) {
      const effectDir = join(nsDir, entry)
      if (!statSync(effectDir).isDirectory()) continue
      const defPath = join(effectDir, 'definition.js')
      if (!existsSync(defPath)) continue
      if (filter && `${namespace}/${entry}` !== filter) continue
      yield { namespace, name: entry, effectDir, defPath }
    }
  }
}

async function main () {
  const argv = process.argv.slice(2)
  const dryRun = argv.includes('--dry-run')
  const filter = argv.find(a => !a.startsWith('--')) || null

  // A full run replaces the whole generated tree, so an effect the reference dropped or
  // renamed cannot linger here.
  if (!dryRun && !filter && existsSync(OUT_DIR)) rmSync(OUT_DIR, { recursive: true, force: true })

  let written = 0
  let failed = 0
  const errors = []
  for (const { namespace, name, effectDir, defPath } of enumerateEffects(filter)) {
    let instance
    try {
      instance = await loadInstance(defPath)
    } catch (err) {
      failed++
      errors.push(`${namespace}/${name}: import failed — ${err?.message || err}`)
      continue
    }
    if (!instance) {
      failed++
      errors.push(`${namespace}/${name}: no default export`)
      continue
    }
    const { func, def } = convertEffect(instance, namespace, name, effectDir)
    if (!dryRun) {
      mkdirSync(join(OUT_DIR, namespace), { recursive: true })
      writeFileSync(join(OUT_DIR, namespace, `${func}.json`), JSON.stringify(def, null, 2) + '\n')
    }
    written++
  }

  const stringsSrc = join(EFFECTS_DIR, STRINGS_FILE)
  if (!filter) {
    if (!existsSync(stringsSrc)) {
      failed++
      errors.push(`missing ${stringsSrc}`)
    } else if (!dryRun) {
      mkdirSync(OUT_DIR, { recursive: true })
      copyFileSync(stringsSrc, join(OUT_DIR, STRINGS_FILE))
    }
  }

  process.stderr.write(`[convert-ui] ${dryRun ? 'would write' : 'wrote'} ${written} effect(s)${filter ? '' : ` + ${STRINGS_FILE}`}, ${failed} failed.\n`)
  for (const e of errors) process.stderr.write(`  ! ${e}\n`)
  if (failed > 0) process.exit(1)
}

if (basename(process.argv[1] || '') === 'convert-effects-ui.mjs') {
  main().catch(err => {
    process.stderr.write(`[convert-ui] FAILED: ${err?.stack || err?.message || JSON.stringify(err)}\n`)
    process.exit(1)
  })
}
