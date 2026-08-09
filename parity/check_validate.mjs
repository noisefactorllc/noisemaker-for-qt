// check_validate.mjs — VALIDATOR parity gate. For every corpus + program DSL, compare nm-render's
// validate() output (candidate, via `nm-render --dump-validated <file>`) against the REFERENCE
// validator (oracle, via tools/dump-validate.mjs), both fed effects/ops/enums/aliases/starters
// from the SAME effect JSONs the way tools/export-graph.mjs does. Full deep value-compare per file
// on success (diagnostics is an ARRAY -- order-sensitive); `ok:false` on both sides counts as a
// pass without comparing error TEXT (matches check_lex.mjs/check_parse.mjs's own contract).
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker NM_RENDER=qt/build-compiler/nm-render \
//     node parity/check_validate.mjs [file...]
//
// Adapted from noisemaker-for-godot/parity/check_validate.mjs: godot spawns ONE Godot process for
// ALL files (via _validate_dump.gd, engine startup dominates); nm-render is a lightweight native
// binary with near-instant startup (each invocation reloads the ~210-file EffectRegistry itself,
// still well under Godot's engine-boot cost), so here the candidate is one
// `nm-render --dump-validated <file>` subprocess PER file -- matches check_lex.mjs/
// check_parse.mjs's own per-file convention (and the T9 brief's literal invocation form). The
// oracle stays batched -- one `node tools/dump-validate.mjs <effectsDir> <all files>` call. The
// reference remains the sole authority; no TD/Godot dependency.
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
if (files.length === 0) {
    files = [...gather(join(REPO, 'parity', 'programs')), ...gather(join(REPO, 'parity', 'corpus'))]
}
files = [...new Set(files.map(f => resolve(f)))].sort()
if (files.length === 0) { console.error('no DSL files found'); process.exit(2) }

if (!existsSync(NM_RENDER)) {
    console.error(`nm-render not found at ${NM_RENDER} (build it first, or set NM_RENDER=<path>)`)
    process.exit(2)
}

const oracle = JSON.parse(execFileSync('node', [join(REPO, 'tools', 'dump-validate.mjs'), EFFECTS_DIR, ...files],
    { encoding: 'utf8', maxBuffer: 1 << 28 }))

const cand = {}
const spawnFailed = []
for (const f of files) {
    try {
        const raw = execFileSync(NM_RENDER, ['--dump-validated', f], { encoding: 'utf8', maxBuffer: 1 << 28 })
        cand[f] = JSON.parse(raw)
    } catch (e) {
        spawnFailed.push(`${f.replace(REPO + '/', '')}: candidate invocation failed (${e.message.split('\n')[0]})`)
    }
}

// Numbers: equal under a tight relative epsilon (Qt's/V8's double
// formatters can differ in the last ULP across a JSON round trip; the live
// runtime never round-trips through JSON) -- same guard as check_parse.mjs.
function numEq(a, b) {
    if (a === b) return true
    return Math.abs(a - b) <= 1e-12 * Math.max(1, Math.abs(a), Math.abs(b))
}
// Key-order-INSENSITIVE deep equality for objects; arrays stay ORDER-
// SENSITIVE (diagnostics is an array -- see task report on Qt's
// QJsonObject alphabetical-not-insertion-order iteration and why this
// matters for the "unknown kwarg" sweep specifically, currently inert on
// this corpus).
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
    if (!o.ok) {
        if (!c.ok) { pass++; continue }
        fail++; failed.push(`${rel}: ref errored but candidate ok`); continue
    }
    if (!c.ok) { fail++; failed.push(`${rel}: candidate errored (${c.error}) but ref ok`); continue }
    if (eq(o.out, c.out)) { pass++; continue }
    fail++
    failed.push(`${rel}: ${firstDiff(o.out, c.out, 'out')}`)
}
console.log(`VALIDATE: ${pass}/${files.length}`)
for (const x of failed.slice(0, 25)) console.log('  DIFF ' + x)
process.exit(fail === 0 ? 0 : 1)
