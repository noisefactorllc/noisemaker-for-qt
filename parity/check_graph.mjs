// check_graph.mjs — GRAPH parity gate. The real target: the in-engine C++ compiler (nm::compileGraph
// / dsl_compiler.cpp) builds the SAME normalized render graph the reference produces (the exact
// shape the Qt renderer consumes — docs/GRAPH-JSON-SCHEMA.md). For every corpus + program DSL,
// compare nm-render's `--dump-graph` output (candidate) against reference normalizeGraph(
// compileGraph(dsl)) (oracle, via tools/dump-graph.mjs), both over the same effect JSONs.
// Key-order-insensitive deep compare; numbers under a tight relative epsilon.
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker NM_RENDER=qt/build-compiler/nm-render \
//     node parity/check_graph.mjs [file...]
//
// Adapted from noisemaker-for-godot/parity/check_graph.mjs (candidate there: a Godot script,
// engine-startup-bound, batched); here nm-render is a native binary with near-instant startup, so —
// matching this repo's own check_lex/parse/validate.mjs convention — the candidate is spawned ONCE
// PER FILE (`nm-render --dump-graph <file>`). The oracle stays batched. Unlike check_expand.mjs, the
// `programs` field here needs NO special-casing: export-graph.mjs's own normalizePrograms() already
// reduces every program entry to just {uniformLayout, defines} (shader source is never part of the
// normalized/GRAPH shape on the reference side either — "the HLSL loader does not need shader
// source"), so both sides compare on equal, meaningful footing without narrowing anything.
import { resolve, dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { readdirSync, existsSync } from 'node:fs'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const NM_RENDER = process.env.NM_RENDER || join(REPO, 'qt', 'build', 'nm-render')
const EFFECTS_DIR = join(REPO, 'qt', 'noisemaker', 'effects')

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }

function gather(dir) {
    if (!existsSync(dir)) return []
    const out = []
    for (const e of readdirSync(dir, { withFileTypes: true })) {
        const p = join(dir, e.name)
        if (e.isDirectory()) out.push(...gather(p))
        else if (e.name.endsWith('.dsl')) out.push(p)
    }
    return out
}

let files = process.argv.slice(2)
if (files.length === 0) files = [...gather(join(REPO, 'parity', 'programs')), ...gather(join(REPO, 'parity', 'corpus'))]
files = [...new Set(files.map(f => resolve(f)))].sort()
if (files.length === 0) { console.error('no DSL files found'); process.exit(2) }

if (!existsSync(NM_RENDER)) {
    console.error(`nm-render not found at ${NM_RENDER} (build it first, or set NM_RENDER=<path>)`)
    process.exit(2)
}

const oracle = JSON.parse(execFileSync('node', [join(REPO, 'tools', 'dump-graph.mjs'), EFFECTS_DIR, ...files],
    { encoding: 'utf8', maxBuffer: 1 << 28 }))

const cand = {}
const spawnFailed = []
for (const f of files) {
    try {
        const raw = execFileSync(NM_RENDER, ['--dump-graph', f], { encoding: 'utf8', maxBuffer: 1 << 28 })
        cand[f] = JSON.parse(raw)
    } catch (e) {
        spawnFailed.push(`${f.replace(REPO + '/', '')}: candidate invocation failed (${e.message.split('\n')[0]})`)
    }
}

function numEq(a, b) {
    if (a === b) return true
    return Math.abs(a - b) <= 1e-12 * Math.max(1, Math.abs(a), Math.abs(b))
}
function eq(a, b) {
    if (a === b) return true
    if (typeof a === 'number' && typeof b === 'number') return numEq(a, b)
    if (a === null || b === null || typeof a !== 'object' || typeof b !== 'object') return a === b
    if (Array.isArray(a) !== Array.isArray(b)) return false
    if (Array.isArray(a)) {
        if (a.length !== b.length) return false
        for (let i = 0; i < a.length; i++) if (!eq(a[i], b[i])) return false
        return true
    }
    const ka = Object.keys(a), kb = Object.keys(b)
    if (ka.length !== kb.length) return false
    for (const k of ka) { if (!(k in b) || !eq(a[k], b[k])) return false }
    return true
}
function firstDiff(a, b, path) {
    if (eq(a, b)) return null
    const ta = a === null ? 'null' : Array.isArray(a) ? 'array' : typeof a
    const tb = b === null ? 'null' : Array.isArray(b) ? 'array' : typeof b
    if (ta !== tb || (ta !== 'object' && ta !== 'array')) return `${path}: ref=${JSON.stringify(a)} mine=${JSON.stringify(b)}`
    if (Array.isArray(a)) {
        if (a.length !== b.length) return `${path}: length ref=${a.length} mine=${b.length}`
        for (let i = 0; i < a.length; i++) { const d = firstDiff(a[i], b[i], `${path}[${i}]`); if (d) return d }
        return `${path}: (array differs)`
    }
    const keys = new Set([...Object.keys(a), ...Object.keys(b)])
    for (const k of keys) {
        if (!(k in a)) return `${path}.${k}: missing in ref (mine=${JSON.stringify(b[k])})`
        if (!(k in b)) return `${path}.${k}: missing in mine (ref=${JSON.stringify(a[k])})`
        const d = firstDiff(a[k], b[k], `${path}.${k}`); if (d) return d
    }
    return `${path}: (object differs)`
}

let pass = 0, fail = 0
const failed = [...spawnFailed]
fail += spawnFailed.length
for (const f of files) {
    if (!(f in cand)) continue // already recorded as a spawn failure above
    const o = oracle[f], c = cand[f]
    const rel = f.replace(REPO + '/', '')
    if (!o) { fail++; failed.push(`${rel}: missing from oracle`); continue }
    if (!o.ok) { if (!c.ok) { pass++; continue } fail++; failed.push(`${rel}: ref errored but candidate ok`); continue }
    if (!c.ok) { fail++; failed.push(`${rel}: candidate errored (${c.error}) but ref ok`); continue }
    if (eq(o.out, c.out)) { pass++; continue }
    fail++
    failed.push(`${rel}: ${firstDiff(o.out, c.out, 'graph')}`)
}
console.log(`GRAPH: ${pass}/${files.length}`)
for (const x of failed.slice(0, 25)) console.log('  DIFF ' + x)
process.exit(fail === 0 ? 0 : 1)
