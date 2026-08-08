// check_shaders.mjs — SHADER-DRIFT gate. Asserts the committed GLSL corpus under
// qt/noisemaker/shaders/effects is exactly what tools/convert-shaders-qt.mjs produces from the
// current reference checkout.
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/check_shaders.mjs
//
// Why this exists: PORTING-GUIDE.md rule 1 requires the shader corpus to be a byte-copy of the
// reference, never a fork. Nothing else in this repo re-derives from the reference checkout on
// every run — a hand-edit or a missed re-sync would sit undetected. This gate re-copies into a
// scratch directory and diffs, so drift is caught, not merely hoped against.
//
// Method: regenerate the full corpus into an EMPTY temp dir (NM_OUT_DIR override), then diff
// against the committed tree, byte for byte, both directions (missing-in-either = failure).
// Unlike check_definitions.mjs, there is no carry-forward state to preserve here — the shader
// converter is a pure function of the reference tree (copy bytes, transform nothing), so seeding
// the temp dir with the committed content first would be pointless; an empty dir is a strictly
// stronger check (it also proves the converter creates every directory from scratch).
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'
import { tmpdir } from 'node:os'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const SHADERS_DIR = join(REPO, 'qt', 'noisemaker', 'shaders', 'effects')

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }

const tmp = mkdtempSync(join(tmpdir(), 'nm-qt-shaders-'))
try {
  execFileSync('node', [join(REPO, 'tools', 'convert-shaders-qt.mjs')],
    { env: { ...process.env, NM_OUT_DIR: tmp }, stdio: ['ignore', 'ignore', 'pipe'], encoding: 'utf8' })

  const walk = (d, base, out = []) => {
    if (!existsSync(d)) return out
    for (const e of readdirSync(d)) {
      const p = join(d, e)
      if (statSync(p).isDirectory()) walk(p, base, out)
      else if (e.endsWith('.frag')) out.push(relative(base, p))
    }
    return out
  }
  const committed = new Set(walk(SHADERS_DIR, SHADERS_DIR))
  const generated = new Set(walk(tmp, tmp))

  const drift = []
  for (const rel of [...new Set([...committed, ...generated])].sort()) {
    if (!committed.has(rel)) { drift.push(`MISSING from repo (reference has it): ${rel}`); continue }
    if (!generated.has(rel)) { drift.push(`STALE in repo (reference dropped/renamed it): ${rel}`); continue }
    const a = readFileSync(join(SHADERS_DIR, rel))
    const b = readFileSync(join(tmp, rel))
    if (!a.equals(b)) drift.push(`DRIFTED: ${rel}`)
  }

  const total = new Set([...committed, ...generated]).size
  const matched = total - drift.length
  if (drift.length === 0) {
    console.log(`SHADERS: ${matched}/${total} byte-identical`)
    process.exit(0)
  }
  console.log(`SHADERS: ${matched}/${total} byte-identical`)
  for (const d of drift) console.log(`  ${d}`)
  console.log('\nFix: NM_REFERENCE_ROOT=... node tools/convert-shaders-qt.mjs')
  process.exit(1)
} finally {
  if (existsSync(tmp)) rmSync(tmp, { recursive: true, force: true })
}
