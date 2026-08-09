// check_lex.mjs — LEXER parity gate. For every corpus + program DSL, compare nm-render's token
// stream (candidate, via `nm-render --dump-tokens <file>`) against the REFERENCE lexer (oracle,
// via tools/dump-tokens.mjs). Deep value-compare per file; report PASS / DIFF + the first
// divergence.
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker NM_RENDER=qt/build/nm-render node parity/check_lex.mjs [file...]
//
// Adapted from noisemaker-for-godot/parity/check_lex.mjs: Godot spawns ONE Godot process for ALL
// files (Godot's engine startup is slow enough that batching matters); nm-render is a lightweight
// native binary with near-instant startup, so here the candidate is one `nm-render --dump-tokens
// <file>` subprocess PER file (also matches the T8 brief's literal invocation form). The oracle
// stays batched -- one `node tools/dump-tokens.mjs <all files>` call, since it already natively
// supports many files per process. The reference remains the sole authority; no TD dependency.
import { resolve, dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { readdirSync, existsSync } from 'node:fs'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const NM_RENDER = process.env.NM_RENDER || join(REPO, 'qt', 'build', 'nm-render')

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

const oracle = JSON.parse(execFileSync('node', [join(REPO, 'tools', 'dump-tokens.mjs'), ...files],
    { encoding: 'utf8', maxBuffer: 1 << 28 }))

const cand = {}
const spawnFailed = []
for (const f of files) {
    try {
        const raw = execFileSync(NM_RENDER, ['--dump-tokens', f], { encoding: 'utf8', maxBuffer: 1 << 28 })
        cand[f] = JSON.parse(raw)
    } catch (e) {
        spawnFailed.push(`${f.replace(REPO + '/', '')}: candidate invocation failed (${e.message.split('\n')[0]})`)
    }
}

// Key-order-INSENSITIVE deep equality -- Qt's QJsonDocument serializes object keys in its own
// (insertion) order, which need not match the reference's JS object key order; a raw string
// compare would false-positive on order alone. (The parser/graph gates need this too.)
function eq(a, b) {
    if (a === b) return true
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

let pass = 0, fail = 0
const failed = [...spawnFailed]
fail += spawnFailed.length
for (const f of files) {
    if (!(f in cand)) continue // already recorded as a spawn failure above
    const o = oracle[f], c = cand[f]
    if (eq(o, c)) { pass++; continue }
    fail++
    let where = `length ref=${o ? o.length : 'missing'} mine=${c ? c.length : 'missing'}`
    const m = Math.max(o ? o.length : 0, c ? c.length : 0)
    for (let k = 0; k < m; k++) {
        if (!eq(o && o[k], c && c[k])) {
            where = `tok#${k} ref=${JSON.stringify(o && o[k])} mine=${JSON.stringify(c && c[k])}`
            break
        }
    }
    failed.push(`${f.replace(REPO + '/', '')}: ${where}`)
}
console.log(`LEX: ${pass}/${files.length}`)
for (const x of failed.slice(0, 25)) console.log('  DIFF ' + x)
process.exit(fail === 0 ? 0 : 1)
