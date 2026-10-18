#!/usr/bin/env node
// fixture-coverage.mjs: regenerates parity/effect-fixture-coverage.json from the reference.
//
// The record says which parity/programs fixtures exercise each authority effect. An effect
// counts as exercised by a fixture when the UNCHANGED reference compiler (tools/export-graph.mjs)
// produces a pass with that effect's key from the fixture's DSL, so search order, aliases and
// chains resolve exactly as the reference resolves them. The authority effect list is the
// reference's shaders/effects/manifest.json. Two fields are curated, not derived:
// authority_backed_exclusions (effects excused by the authority, each with its reason) and
// headless_signal_input_scope (what the headless runs feed the signal-input effects). They are
// carried over from the committed record unchanged.
//
// Usage:
//   NM_REFERENCE_ROOT=<ref> node tools/fixture-coverage.mjs --reference-commit <sha> [--write | --check]
//
//   --write  rewrite parity/effect-fixture-coverage.json
//   --check  exit 1 unless the committed record equals the regenerated one (the default)
//
// The reference checkout must be at --reference-commit; scripts/test passes its pinned PIN.
// The script exits 1, before it reads the reference, when the checkout HEAD is another commit.

import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { readFileSync, readdirSync, writeFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { exportGraph } from './export-graph.mjs'

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const recordPath = join(repoRoot, 'parity', 'effect-fixture-coverage.json')
const programsDir = join(repoRoot, 'parity', 'programs')

function parseArgs (argv) {
  const args = { mode: 'check', referenceCommit: null }
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--write') args.mode = 'write'
    else if (argv[i] === '--check') args.mode = 'check'
    else if (argv[i] === '--reference-commit') args.referenceCommit = argv[++i]
    else throw new Error(`unknown argument: ${argv[i]}`)
  }
  if (!/^[0-9a-f]{40}$/.test(args.referenceCommit ?? '')) throw new Error('--reference-commit needs a full 40-hex commit')
  return args
}

export async function buildRecord ({ referenceRoot, referenceCommit, committed }) {
  const manifestBytes = readFileSync(join(referenceRoot, 'shaders', 'effects', 'manifest.json'))
  const authority = Object.keys(JSON.parse(manifestBytes)).map((id) => id.replace('/', '.')).sort()
  const excluded = new Set((committed.authority_backed_exclusions ?? []).map((entry) => entry.effect ?? entry))
  const fixtures = readdirSync(programsDir).filter((name) => name.endsWith('.dsl')).sort()
  const covered = new Map(authority.map((id) => [id, []]))
  const failures = []
  let compiled = 0
  for (const fixture of fixtures) {
    let graph
    try {
      graph = await exportGraph(readFileSync(join(programsDir, fixture), 'utf8'))
    } catch (error) {
      failures.push(`${fixture}: ${error?.message ?? error}`)
      continue
    }
    compiled++
    for (const key of new Set(graph.passes.map((pass) => pass.effectKey).filter(Boolean))) {
      if (covered.has(key)) covered.get(key).push(fixture)
    }
  }
  const withFixture = authority.filter((id) => covered.get(id).length > 0)
  return {
    record: {
      reference_commit: referenceCommit,
      reference_manifest_sha256: createHash('sha256').update(manifestBytes).digest('hex'),
      fixture_count: fixtures.length,
      compiled_fixture_graph_count: compiled,
      authority_effect_count: authority.length,
      effects_with_fixture_count: withFixture.length,
      effects_with_no_fixture: authority.filter((id) => covered.get(id).length === 0 && !excluded.has(id)),
      authority_backed_exclusions: committed.authority_backed_exclusions ?? [],
      headless_signal_input_scope: committed.headless_signal_input_scope ?? {},
      effect_to_fixtures: Object.fromEntries(withFixture.map((id) => [id, covered.get(id)])),
    },
    failures,
  }
}

export function referenceHead (referenceRoot) {
  const result = spawnSync('git', ['-C', referenceRoot, 'rev-parse', '--verify', 'HEAD^{commit}'], { encoding: 'utf8' })
  return result.status === 0 ? result.stdout.trim() : null
}

async function main () {
  const args = parseArgs(process.argv.slice(2))
  if (!process.env.NM_REFERENCE_ROOT) throw new Error('NM_REFERENCE_ROOT is not set')
  const head = referenceHead(resolve(process.env.NM_REFERENCE_ROOT))
  if (head !== args.referenceCommit) {
    throw new Error(`reference checkout ${resolve(process.env.NM_REFERENCE_ROOT)} is at HEAD ` +
      `${head ?? '(no git revision)'}, not at --reference-commit ${args.referenceCommit}`)
  }
  const committedText = readFileSync(recordPath, 'utf8')
  const { record, failures } = await buildRecord({
    referenceRoot: resolve(process.env.NM_REFERENCE_ROOT),
    referenceCommit: args.referenceCommit,
    committed: JSON.parse(committedText),
  })
  const text = JSON.stringify(record, null, 2) + '\n'
  for (const failure of failures) console.error(`COVERAGE compile failure ${failure}`)
  const summary = `COVERAGE fixtures=${record.fixture_count} compiled=${record.compiled_fixture_graph_count} ` +
    `authority=${record.authority_effect_count} covered=${record.effects_with_fixture_count} ` +
    `uncovered=${record.effects_with_no_fixture.length} excluded=${record.authority_backed_exclusions.length}`
  if (args.mode === 'write') {
    writeFileSync(recordPath, text)
    console.log(`${summary} wrote ${recordPath}`)
    return
  }
  const fresh = text === committedText
  console.log(`${summary} record=${fresh ? 'current' : 'STALE'}`)
  if (!fresh || failures.length || record.effects_with_no_fixture.length) {
    if (!fresh) console.error('parity/effect-fixture-coverage.json differs from the regenerated record; run with --write and review the diff')
    process.exitCode = 1
  }
}

main().catch((error) => {
  console.error(error?.stack ?? String(error))
  process.exitCode = 1
})
