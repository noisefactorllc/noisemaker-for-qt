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
import { cpSync, existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'
import { tmpdir } from 'node:os'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const EFFECTS_DIR = join(REPO, 'qt', 'noisemaker', 'effects')

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }

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
