// check_effects_ui.mjs — UI-METADATA DRIFT gate. Asserts the committed sidecar tree under
// qt/noisemaker/effects-ui is exactly what tools/convert-effects-ui.mjs produces from the current
// reference checkout.
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/check_effects_ui.mjs
//
// Unlike check_definitions.mjs this regenerates into an EMPTY temp dir: the sidecar carries no
// port-authored data forward, so an empty-dir regeneration is the complete expected tree, and a
// reference-side deletion or rename shows up as a STALE committed file without a second pass.
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'
import { tmpdir } from 'node:os'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const UI_DIR = join(REPO, 'qt', 'noisemaker', 'effects-ui')

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }

const walk = (d, base, out = []) => {
  if (!existsSync(d)) return out
  for (const e of readdirSync(d)) {
    const p = join(d, e)
    if (statSync(p).isDirectory()) walk(p, base, out)
    else if (e.endsWith('.json')) out.push(relative(base, p))
  }
  return out
}

const tmp = mkdtempSync(join(tmpdir(), 'nm-qt-ui-'))
try {
  const out = join(tmp, 'effects-ui')
  execFileSync('node', [join(REPO, 'tools', 'convert-effects-ui.mjs')],
    { env: { ...process.env, NM_OUT_DIR: out }, stdio: ['ignore', 'ignore', 'pipe'], encoding: 'utf8' })

  const committed = new Set(walk(UI_DIR, UI_DIR))
  const generated = new Set(walk(out, out))
  if (committed.size === 0 && generated.size === 0) {
    console.error('EFFECTS_UI: 0/0 — empty tree on both sides; refusing a vacuous pass (check NM_REFERENCE_ROOT)')
    process.exit(3)
  }

  const drift = []
  for (const rel of [...new Set([...committed, ...generated])].sort()) {
    if (!committed.has(rel)) { drift.push(`MISSING from repo (reference has it): ${rel}`); continue }
    if (!generated.has(rel)) { drift.push(`STALE in repo (reference dropped/renamed it): ${rel}`); continue }
    if (readFileSync(join(UI_DIR, rel), 'utf8') !== readFileSync(join(out, rel), 'utf8')) drift.push(`DRIFTED: ${rel}`)
  }

  const total = new Set([...committed, ...generated]).size
  console.log(`EFFECTS_UI: ${total - drift.length}/${total} byte-identical`)
  if (drift.length === 0) process.exit(0)
  for (const d of drift) console.log(`  ${d}`)
  console.log('\nFix: NM_REFERENCE_ROOT=... node tools/convert-effects-ui.mjs')
  process.exit(1)
} finally {
  if (existsSync(tmp)) rmSync(tmp, { recursive: true, force: true })
}
