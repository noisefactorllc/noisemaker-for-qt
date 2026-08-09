// check_parse.mjs — PARSER parity gate. For every corpus + program DSL, compare nm-render's
// Program AST (candidate, via `nm-render --dump-ast <file>`) against the REFERENCE parser
// (oracle, via tools/dump-ast.mjs). Key-order-insensitive deep value-compare per file; report
// PASS / DIFF + the first divergence path.
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker NM_RENDER=qt/build/nm-render node parity/check_parse.mjs [file...]
//
// Adapted from noisemaker-for-godot/parity/check_parse.mjs -- see check_lex.mjs's header comment
// for why the candidate here is spawned once PER FILE (nm-render's near-instant native startup)
// rather than Godot's one-process-for-everything batching. The reference remains the sole
// authority; no TD dependency.
import { resolve, dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { readdirSync, existsSync } from 'node:fs'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const NM_RENDER = process.env.NM_RENDER || join(REPO, 'qt', 'build', 'nm-render')

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

const oracle = JSON.parse(execFileSync('node', [join(REPO, 'tools', 'dump-ast.mjs'), ...files],
    { encoding: 'utf8', maxBuffer: 1 << 28 }))

const cand = {}
const spawnFailed = []
for (const f of files) {
    try {
        // nm-render's --dump-ast always exits 0 and prints {ok,...} JSON,
        // even for a DSL syntax error (mirrors dump-ast.mjs's own
        // try/catch) -- a thrown/non-zero exit here means the TOOL itself
        // failed (bad path, crash), not that the file has a syntax error.
        const raw = execFileSync(NM_RENDER, ['--dump-ast', f], { encoding: 'utf8', maxBuffer: 1 << 28 })
        cand[f] = JSON.parse(raw)
    } catch (e) {
        spawnFailed.push(`${f.replace(REPO + '/', '')}: candidate invocation failed (${e.message.split('\n')[0]})`)
    }
}

// Numbers: equal under a tight relative epsilon. nm::parse computes the SAME IEEE-754 doubles as
// the reference (parse-time constant folding in `double` end-to-end -- PORTING-GUIDE.md), but this
// guards against any last-bit JSON-text-roundtrip noise between Qt's and V8's double formatters
// (a harness/serialization concern; the live runtime never round-trips through JSON). 1e-12
// relative is tight enough to still catch any genuine value difference.
function numEq(a, b) {
    if (a === b) return true
    return Math.abs(a - b) <= 1e-12 * Math.max(1, Math.abs(a), Math.abs(b))
}

// Key-order-INSENSITIVE deep equality (Qt's QJsonDocument serializes object keys in its own
// order; arrays stay ordered).
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
    if (ta !== tb || (ta !== 'object' && ta !== 'array')) {
        return `${path}: ref=${JSON.stringify(a)} mine=${JSON.stringify(b)}`
    }
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
        // Reference rejected this file as a syntax error; candidate should
        // too (the valid corpus should never land here). Agreement on
        // rejection counts as a pass; the exact error TEXT is not diffed
        // (see task report -- DslSyntaxError message parity is a design
        // goal but not machine-gated by this script, matching dump-ast.mjs's
        // own ok:true/false-only comparison contract).
        if (!c.ok) { pass++; continue }
        fail++; failed.push(`${rel}: ref rejected (syntax error) but candidate accepted`); continue
    }
    if (!c.ok) { fail++; failed.push(`${rel}: candidate rejected (${c.error}) but ref accepted`); continue }
    if (eq(o.ast, c.ast)) { pass++; continue }
    fail++
    failed.push(`${rel}: ${firstDiff(o.ast, c.ast, 'ast')}`)
}
console.log(`PARSE: ${pass}/${files.length}`)
for (const x of failed.slice(0, 25)) console.log('  DIFF ' + x)
process.exit(fail === 0 ? 0 : 1)
