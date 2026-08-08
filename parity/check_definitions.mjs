// check_definitions.mjs — DEFINITION-DRIFT gate. Asserts the committed effect JSONs under
// qt/noisemaker/effects are exactly what tools/convert-definitions.mjs produces from the current
// reference checkout.
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/check_definitions.mjs
//
// Why this exists: gates that diff committed artifacts against OTHER committed artifacts (a
// future compiler-side registry/dump gate fed these same JSONs, for instance) cannot see this
// class of drift — a stale input is identical on both sides of that comparison and passes
// trivially. This is the only gate that re-derives from the reference checkout itself. The
// Godot port (this file's ancestor, noisemaker-for-godot/parity/check_definitions.mjs) shipped
// exactly this failure mode: 31 effects silently fell behind the reference (new `artist` tags,
// reworded descriptions, a packing-constant change, a rename, a missing effect) with every one
// of its other gates green, because none of them re-read the reference.
//
// Method: copy the committed tree to a temp dir, regenerate INTO that copy, diff. Regenerating into
// a copy (not an empty dir) is required — the generator carries port-authored `uniformLayouts`
// forward from the file it replaces, and those exist for effects whose packing layout the
// reference does not declare. Regenerating into an empty dir would drop them and this gate would
// report false drift.
//
// Fix round 1 (reviewer finding): that same seeding is a blind spot for a PURE reference-side
// deletion. `tmp` starts as a copy of `committed`, and convert-definitions.mjs only ever WRITES
// files for effects it currently enumerates — it never unlinks a stale one. So if a reference
// definition.js is deleted outright, the seeded (stale) copy just sits untouched in `tmp` too:
// `generated.has(rel)` is trivially true (cpSync put it there), the two copies are byte-identical
// (neither was touched), and the byte-diff loop below reports no drift at all. The "STALE in repo"
// branch in that loop is effectively unreachable for this specific failure mode under this seeding
// scheme — it only fires for shapes this walk can't actually produce today. The second pass below
// closes the gap: it is INDEPENDENT of the regenerated temp dir entirely, walking the reference
// tree directly to build the live "ns/func with a definition.js right now" set, and flags any
// committed relpath outside it. (Not reusing convert-definitions.mjs's own enumeration — an
// independent census also catches a bug in that enumeration itself, not just reference deletions.)
import { cpSync, existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs'
import { basename, dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'
import { tmpdir } from 'node:os'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const EFFECTS_DIR = join(REPO, 'qt', 'noisemaker', 'effects')

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }
const REFERENCE_ROOT = resolve(process.env.NM_REFERENCE_ROOT)
const REF_EFFECTS_DIR = join(REFERENCE_ROOT, 'shaders', 'effects')

function isDir (p) {
  try { return statSync(p).isDirectory() } catch { return false }
}

// Independent live-effect census (fix round 1): ns/func pairs the reference currently declares
// a definition.js for. Namespaces auto-discovered (not hardcoded), same technique as
// convert-shaders-qt.mjs — deliberately not sharing convert-definitions.mjs's own namespace list,
// so a bug in that list can't blind this check the same way.
function liveReferenceEffects () {
  const live = new Set()
  for (const ns of readdirSync(REF_EFFECTS_DIR)) {
    const nsDir = join(REF_EFFECTS_DIR, ns)
    if (!isDir(nsDir)) continue // skip manifest.json, strings.*.json, HELP_TEMPLATE.md
    for (const func of readdirSync(nsDir)) {
      const funcDir = join(nsDir, func)
      if (!isDir(funcDir)) continue
      if (existsSync(join(funcDir, 'definition.js'))) live.add(`${ns}/${func}`)
    }
  }
  return live
}

const tmp = mkdtempSync(join(tmpdir(), 'nm-qt-defs-'))
try {
  cpSync(EFFECTS_DIR, tmp, { recursive: true })
  execFileSync('node', [join(REPO, 'tools', 'convert-definitions.mjs')],
    { env: { ...process.env, NM_OUT_DIR: tmp }, stdio: ['ignore', 'ignore', 'pipe'], encoding: 'utf8' })

  const walk = (d, base, out = []) => {
    for (const e of readdirSync(d)) {
      const p = join(d, e)
      if (statSync(p).isDirectory()) walk(p, base, out)
      else if (e.endsWith('.json')) out.push(relative(base, p))
    }
    return out
  }
  const committed = new Set(walk(EFFECTS_DIR, EFFECTS_DIR))
  const generated = new Set(walk(tmp, tmp))

  // Adjacent hardening (fix round 1): an empty tree on both sides would otherwise print a vacuous
  // "DEFINITIONS: 0/0 byte-identical" and exit 0 — a silent false pass if NM_REFERENCE_ROOT or
  // EFFECTS_DIR is misconfigured badly enough that neither side finds anything.
  if (committed.size === 0 && generated.size === 0) {
    console.error('DEFINITIONS: 0/0 — empty tree on both sides; refusing a vacuous pass (check NM_REFERENCE_ROOT and EFFECTS_DIR)')
    process.exit(3)
  }

  const drift = []
  for (const rel of [...new Set([...committed, ...generated])].sort()) {
    // NOTE: a case-only rename in the reference is invisible on a case-insensitive filesystem
    // (macOS default); compare names case-sensitively so it still surfaces.
    if (!committed.has(rel)) { drift.push(`MISSING from repo (reference has it): ${rel}`); continue }
    if (!generated.has(rel)) { drift.push(`STALE in repo (reference dropped/renamed it): ${rel}`); continue }
    const a = readFileSync(join(EFFECTS_DIR, rel), 'utf8')
    const b = readFileSync(join(tmp, rel), 'utf8')
    if (a !== b) drift.push(`DRIFTED: ${rel}`)
  }

  // Second, independent pass (fix round 1): catches the pure-deletion case the byte-diff loop
  // above cannot see (see the file-level comment). Committed relpaths are `<ns>/<func>.json`
  // flat — verified against today's tree (no nested namespaces) — so ns is the first path
  // segment and func is the basename minus `.json`.
  const live = liveReferenceEffects()
  const flaggedAlready = new Set(drift.map(d => d.slice(d.lastIndexOf(' ') + 1)))
  for (const rel of committed) {
    const slash = rel.indexOf('/')
    const ns = rel.slice(0, slash)
    const func = basename(rel.slice(slash + 1), '.json')
    if (!live.has(`${ns}/${func}`) && !flaggedAlready.has(rel)) {
      drift.push(`STALE in repo (reference dropped/renamed it): ${rel}`)
    }
  }

  const total = new Set([...committed, ...generated]).size
  const matched = total - drift.length
  if (drift.length === 0) {
    console.log(`DEFINITIONS: ${matched}/${total} byte-identical`)
    process.exit(0)
  }
  console.log(`DEFINITIONS: ${matched}/${total} byte-identical`)
  for (const d of drift) console.log(`  ${d}`)
  console.log('\nFix: NM_REFERENCE_ROOT=... node tools/convert-definitions.mjs')
  console.log('(a case-only rename needs `git mv` first — macOS will not rename it for you)')
  process.exit(1)
} finally {
  if (existsSync(tmp)) rmSync(tmp, { recursive: true, force: true })
}
