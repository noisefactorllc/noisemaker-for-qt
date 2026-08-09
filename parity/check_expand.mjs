// check_expand.mjs — EXPANDER parity gate. For every corpus + program DSL, compare the REFERENCE
// expander's RAW output (oracle, via tools/dump-expand.mjs) against nm::expand() (candidate, via
// `expand_dump <file>` — a standalone binary, NOT an nm-render flag; see expand_dump.cpp's header
// for why: flag_hooks.h's kPreDeclaredFlags table (frozen since T3) has no --dump-expand slot).
// Key-order-insensitive deep compare per file; numbers under a tight relative epsilon (matches
// check_parse.mjs/check_validate.mjs's own JSON-round-trip safety net).
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker NM_EXPAND_DUMP=qt/build-compiler/tests/expand_dump \
//     node parity/check_expand.mjs [file...]
//
// Adapted from noisemaker-for-godot/parity/check_expand.mjs: godot spawns ONE process for the WHOLE
// corpus (its candidate is a Godot script, engine-startup-bound); here the candidate is a native
// binary with near-instant startup, so — matching this repo's own check_lex/parse/validate.mjs
// convention — it is spawned ONCE PER FILE. The oracle stays batched (one
// `node tools/dump-expand.mjs <effectsDir> <all files>` call).
//
// `programs` field handling (deliberate, DOCUMENTED narrowing — read before assuming this "weakens"
// the gate): this repo's converted effect JSONs (qt/noisemaker/effects/**) carry NO `shaders` key at
// all (verified directly: every `programs` entry the live oracle itself produces for this catalog is
// EITHER the single built-in "blit" program OR — when an effect JSON did have a `shaders` field —
// would carry inline GLSL/WGSL source; this port's own architecture stores shader source in separate
// .glsl files on disk instead, resolved by (namespace,func,progName) at RENDER time — see
// docs/GRAPH-JSON-SCHEMA.md "Qt consumer": "the backend does NOT read shader source from here"). The
// reference's own `ensureBlitProgram()` therefore contributes fixed, INERT template-literal shader
// text (fragment/wgsl/fragmentEntryPoint) that has zero bearing on this port's correctness and is
// never read by nm::Backend. This gate compares the `programs` field by KEY SET only (which program
// cache-ids exist — itself a strong, corpus-wide check of the compile-time-define suffix algorithm,
// since a wrong suffix produces a wrong/missing key), not by VALUE — the numeric/type correctness of
// each program's `defines`/`uniformLayout` is independently and MORE rigorously verified via (a) each
// pass's own `program` string (which ENCODES every define value in its suffix, e.g.
// `__NOISE_TYPE_10`, and IS fully value-compared below) and (b) parity/check_graph.mjs's
// normalizePrograms()/definesForPass()/define-promotion comparison. Every other field — passes,
// errors, textureSpecs, renderSurface, and each pass's own inputs/outputs/uniforms/uniformSpecs/
// metadata — is compared in full.
import { resolve, dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { readdirSync, existsSync } from 'node:fs'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const EXPAND_DUMP = process.env.NM_EXPAND_DUMP || join(REPO, 'qt', 'build', 'tests', 'expand_dump')
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

if (!existsSync(EXPAND_DUMP)) {
    console.error(`expand_dump not found at ${EXPAND_DUMP} (build it first, or set NM_EXPAND_DUMP=<path>)`)
    process.exit(2)
}

const oracle = JSON.parse(execFileSync('node', [join(REPO, 'tools', 'dump-expand.mjs'), EFFECTS_DIR, ...files],
    { encoding: 'utf8', maxBuffer: 1 << 28 }))

const cand = {}
const spawnFailed = []
for (const f of files) {
    try {
        const raw = execFileSync(EXPAND_DUMP, [f], { encoding: 'utf8', maxBuffer: 1 << 28 })
        cand[f] = JSON.parse(raw)
    } catch (e) {
        spawnFailed.push(`${f.replace(REPO + '/', '')}: candidate invocation failed (${e.message.split('\n')[0]})`)
    }
}

// See header: `programs` is compared by KEY SET only. Applied identically
// to both sides before the generic deep-eq below.
function stripProgramValues(out) {
    if (!out || typeof out !== 'object' || !out.programs || typeof out.programs !== 'object') return out
    return { ...out, programs: Object.keys(out.programs).sort() }
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
    const oNorm = stripProgramValues(o.out), cNorm = stripProgramValues(c.out)
    if (eq(oNorm, cNorm)) { pass++; continue }
    fail++
    failed.push(`${rel}: ${firstDiff(oNorm, cNorm, 'out')}`)
}
console.log(`EXPAND: ${pass}/${files.length}`)
for (const x of failed.slice(0, 25)) console.log('  DIFF ' + x)
process.exit(fail === 0 ? 0 : 1)
