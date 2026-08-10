#!/usr/bin/env node
// batch-golden.mjs — BATCH golden minter for the corpus-wide sweep (task T6).
//
// parity/export-and-render.mjs (unmodified, do not touch — see its own header)
// mints ONE program's golden by launching a fresh BrowserSession, loading the
// DSL, and capturing. That is the VERIFIED per-fixture protocol. Launching a
// fresh Chromium per fixture for a 335-program corpus is what this file
// avoids: it opens ONE BrowserSession (or a small number, see --chunk-size)
// and reuses the SAME page across many DSL loads by driving the exact same
// "set editor text -> click run -> wait for graph.id to change" interaction
// export-and-render.mjs already uses for its own single DSL swap (the demo's
// own supported "load a different program" interaction, exercised here
// repeatedly instead of once).
//
// Every per-fixture step below — resize/pin, the o0-size poll, the
// round-3 state-respawn clear, the 8-frame pinned-time render, and capture()
// itself — is copied VERBATIM from export-and-render.mjs so the protocol is
// byte-identical to the verified single-fixture path (task-T6-brief.md).
// Only the session bootstrap (browser launch, initial page load, backend
// select, initial "is the demo ready" wait) is hoisted OUTSIDE the per-
// fixture loop, since that part of the protocol is page-lifetime scoped, not
// per-DSL scoped, in the single-fixture script too (it happens once before
// that script's own one-and-only DSL load).
//
// Only the runSeconds===0 (single pinned-time, 8-frame) capture mode is
// implemented here. Timed-sampling fixtures (navierStokes, temporalAberration)
// keep using export-and-render.mjs directly, one process each — see
// parity/sweep.sh — since that mode's own capture loop is a different shape
// (many samples, not a single PNG) and isn't part of this batch's scope.
//
// Usage:
//   node batch-golden.mjs <outDir> --list <namesFile> [--size 256] [--time 0.25] \
//        [--backend webgl2] [--chunk-size 60]
//   node batch-golden.mjs <outDir> [options] -- prog1.dsl prog2.dsl ...
//
// <namesFile>: one DSL path per line (blank lines and #comment lines skipped).
// Trailing positional args (after a literal `--`, or just trailing bare args)
// are additional DSL paths, for ad hoc re-minting of a few flaked fixtures.
//
// Writes <outDir>/<programName>.golden.png and <outDir>/<programName>.graph.json
// per fixture. Prints one progress line per fixture to stderr and a final
// summary + terminal "DONE" marker to stdout, so a caller can `tee` to a log
// and grep for progress / detect completion.
//
// Exit code: 0 if every fixture minted; 1 if any did not (summary lists which
// — re-run this tool with just those names to re-mint, per the project's
// same-run "if a mint flakes, re-mint that fixture" rule; never fall back to
// a stale golden from a previous run).
//
// KNOWN FLAKE (task-T6-report.md): one corpus fixture, `scratches` (filter/
// scratches -- its "overlayTex" appears to be populated by a mechanism not
// visible in its own single-pass graph, unlike every other fixture checked),
// has been observed to mint a golden here that differs from the verified
// single-fixture (export-and-render.mjs) mint by ~0.04% of pixels (24/65536)
// on one sweep out of three otherwise-identical full-corpus runs -- not
// reproduced on request (three independent single-fixture mints of the same
// DSL were bit-identical), and not observed on ANY other fixture cross-
// checked (all 45 NEAR/CHAOS-classified fixtures from that same run, and a
// spot-check of PASS fixtures, matched their single-fixture mint exactly).
// Likely session-history-dependent non-determinism inside the reference's
// own rendering for this one effect, not a bug in this file's protocol
// (the CANDIDATE render from the "bad" golden's own graph.json was itself
// bit-identical to the candidate from a verified-good graph.json -- the
// divergence is upstream of anything this file controls). Mitigation: after
// a full sweep, cross-check any NEAR/CHAOS-classified fixture's golden
// against a fresh single-fixture mint before trusting it for a committed
// ledger; replace with the single-fixture version on any mismatch. Not
// automated here (a full N-way cross-check on every run would erase most of
// the batching speedup); do it by hand for the fixtures a sweep actually
// classifies as sensitive to the exact number, the way task-T6 did.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve, basename, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { deflateSync } from 'node:zlib'

const __dirname = dirname(fileURLToPath(import.meta.url))
if (!process.env.NM_REFERENCE_ROOT) {
  throw new Error('NM_REFERENCE_ROOT is not set. Point it at a checkout of the Noisemaker\n'
    + 'reference repo (the JS/WebGL2 engine), e.g. NM_REFERENCE_ROOT=/path/to/noisemaker.')
}
const REFERENCE_ROOT = resolve(process.env.NM_REFERENCE_ROOT)
const HARNESS = join(REFERENCE_ROOT, 'vendor', 'shade-mcp', 'harness', 'index.js')
const EXPORT_GRAPH = join(__dirname, '..', 'tools', 'export-graph.mjs')

// ---- PNG encoder (verbatim copy of export-and-render.mjs's own — see that
// file's header for why: no external npm dependency). ------------------------
function crc32 (buf) {
  let c = 0xffffffff
  for (let i = 0; i < buf.length; i++) {
    c ^= buf[i]
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1)
  }
  return (c ^ 0xffffffff) >>> 0
}
function pngChunk (type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0)
  const typeBuf = Buffer.from(type, 'ascii')
  const body = Buffer.concat([typeBuf, data])
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body), 0)
  return Buffer.concat([len, body, crc])
}
function encodePng (width, height, rgbaTopDown) {
  const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])
  const ihdr = Buffer.alloc(13)
  ihdr.writeUInt32BE(width, 0)
  ihdr.writeUInt32BE(height, 4)
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0
  const raw = Buffer.alloc(height * (1 + width * 4))
  for (let y = 0; y < height; y++) {
    const di = y * (1 + width * 4)
    raw[di] = 0
    rgbaTopDown.copy(raw, di + 1, y * width * 4, (y + 1) * width * 4)
  }
  const idat = deflateSync(raw)
  return Buffer.concat([sig, pngChunk('IHDR', ihdr), pngChunk('IDAT', idat), pngChunk('IEND', Buffer.alloc(0))])
}

// Viewer wiring — identical to export-and-render.mjs.
const VIEWER_ROOT = REFERENCE_ROOT
const VIEWER_PATH = '/demo/shaders/'
const EFFECTS_DIR = join(REFERENCE_ROOT, 'shaders', 'effects')
const GLOBALS_PREFIX = '__noisemaker'
const STATUS_TIMEOUT = 300000

// capture() — verbatim copy of export-and-render.mjs's own.
async function capture (page, globals) {
  const result = await page.evaluate(({ g }) => {
    const pipeline = window[g.renderingPipeline]
    if (!pipeline) return { status: 'error', error: 'no pipeline' }
    const backend = pipeline.backend
    const gl = backend?.gl
    const surface = pipeline.surfaces?.get(pipeline.graph?.renderSurface || 'o0')
    if (!gl || !surface) return { status: 'error', error: 'no GL surface' }
    const info = backend.textures?.get(surface.read)
    if (!info?.handle) return { status: 'error', error: 'no texture handle' }
    const { handle, width, height, glFormat } = info
    const fbo = gl.createFramebuffer()
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo)
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, handle, 0)
    if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.deleteFramebuffer(fbo)
      return { status: 'error', error: 'FBO incomplete' }
    }
    const canFloat = !!(gl.getExtension('EXT_color_buffer_float') || gl.getExtension('WEBGL_color_buffer_float'))
    const isFloat = glFormat?.type === gl.HALF_FLOAT || glFormat?.type === gl.FLOAT
    gl.finish()
    let rgba8
    if (isFloat && canFloat) {
      const buf = new Float32Array(width * height * 4)
      gl.readPixels(0, 0, width, height, gl.RGBA, gl.FLOAT, buf)
      rgba8 = new Array(width * height * 4)
      for (let i = 0; i < buf.length; i++) {
        rgba8[i] = Math.max(0, Math.min(255, Math.round(buf[i] * 255)))
      }
    } else {
      const buf = new Uint8Array(width * height * 4)
      gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, buf)
      rgba8 = Array.from(buf)
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null)
    gl.deleteFramebuffer(fbo)
    return { status: 'ok', width, height, pixels: rgba8 }
  }, { g: globals })
  if (result.status === 'error') throw new Error(`readback failed: ${result.error}`)
  const { width, height, pixels } = result
  const topDown = Buffer.alloc(width * height * 4)
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const src = ((height - 1 - y) * width + x) * 4
      const dst = (y * width + x) * 4
      topDown[dst] = pixels[src]
      topDown[dst + 1] = pixels[src + 1]
      topDown[dst + 2] = pixels[src + 2]
      topDown[dst + 3] = pixels[src + 3]
    }
  }
  return encodePng(width, height, topDown)
}

// Mints ONE program's golden on an already-set-up `page`. Verbatim copy of
// export-and-render.mjs's own runSeconds===0 body (resize/pin, o0-size poll,
// round-3 state-respawn clear, 8-frame render, capture) plus its own
// DSL-swap wait, just factored into a function called once per fixture
// instead of once per process -- PLUS one addition beyond the single-
// fixture protocol, documented here since it's a real behavioral delta:
//
// BATCH-SPECIFIC FIX: the demo's compile is a TWO-PHASE async process --
// `pipeline.graph.id` changes almost immediately (~5-10ms) to a TRANSIENT
// value while `pipeline.graph.passes` still reflects the PREVIOUS graph,
// then `id` changes AGAIN (~30-40ms later) once `passes` actually finishes
// repopulating. Directly observed via instrumented polling (task-T6-report.md):
// loading physarumNoSense (13 passes) right after noise (2 passes) in one
// page session showed `id` change to an intermediate value at t=7ms with
// `passes.length` STILL 2, then change AGAIN at t=43ms with `passes.length`
// correctly 13. export-and-render.mjs's own single-swap-per-process wait
// (`id !== base` alone) never observes this: a cold page's boot + networkidle
// wait already burns far more than 40ms before its one explicit swap begins,
// so the race window is always closed by the time it checks. A warm page
// doing back-to-back swaps has no such cushion. Fix: wait for `id !== base`
// AND `passes.length === expectedPassCount` (the count `exportGraph()`
// already computed on the Node side for this exact DSL, BEFORE the browser
// step -- a ground-truth value, not a guess), then do one short settle
// recheck and warn (not fail) if it flickers again. This makes the wait
// STRICTER than the single-fixture protocol, never different in a case that
// was already passing -- not a change to what "correct" means, a fix for a
// timing assumption the single-fixture protocol gets for free from its own
// slower per-process startup.
async function mintOne (page, globals, opts, dsl, expectedPassCount, programName) {
  const baselineId = await page.evaluate(() =>
    window.__noisemakerRenderingPipeline?.graph?.id ?? null)
  await page.evaluate((src) => {
    const editor = document.getElementById('dsl-editor')
    const runBtn = document.getElementById('dsl-run-btn')
    editor.value = src
    editor.dispatchEvent(new Event('input', { bubbles: true }))
    runBtn.click()
  }, dsl)
  await page.waitForFunction((args) => {
    const { base, expectedPassCount } = args
    const s = (document.getElementById('status')?.textContent || '').toLowerCase()
    if (s.includes('error') || s.includes('failed')) {
      throw new Error('DSL compile failed: ' + document.getElementById('status')?.textContent)
    }
    const p = window.__noisemakerRenderingPipeline
    if (!(p && p.graph && p.graph.id !== base)) return false
    if (typeof expectedPassCount === 'number' && p.graph.passes?.length !== expectedPassCount) return false
    return true
  }, { timeout: STATUS_TIMEOUT }, { base: baselineId, expectedPassCount })

  // Settle recheck: confirm the id we just resolved on is still current a
  // beat later. Never observed to flicker a second time in testing; this is
  // a cheap belt-and-suspenders check, not a load-bearing wait -- if it DOES
  // fire, that's new evidence the two-phase pattern above isn't the whole
  // story, so it's logged loudly rather than silently retried.
  await new Promise((resolve) => setTimeout(resolve, 50))
  const settled = await page.evaluate(() => {
    const p = window.__noisemakerRenderingPipeline
    return { id: p?.graph?.id, passes: p?.graph?.passes?.length }
  })
  if (settled.passes !== expectedPassCount) {
    process.stderr.write(`[batch-golden] WARNING: ${programName}: passes count changed again after settle ` +
      `(expected ${expectedPassCount}, now ${settled.passes}) -- possible further race, re-minting this fixture is recommended\n`)
  }

  await page.evaluate(() => {
    if (window.__noisemakerSetPaused) window.__noisemakerSetPaused(true)
  })

  await page.evaluate((size) => {
    const r = window.__noisemakerCanvasRenderer
    const p = window.__noisemakerRenderingPipeline
    const canvas = r && r.canvas
    if (canvas) {
      const pin = (prop) => {
        Object.defineProperty(canvas, prop, {
          configurable: true,
          enumerable: true,
          get () { return size },
          set () { /* locked to `size` for deterministic capture */ }
        })
      }
      const proto = Object.getPrototypeOf(canvas)
      const wd = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, 'width')
      const hd = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, 'height')
      if (wd && wd.set) wd.set.call(canvas, size)
      if (hd && hd.set) hd.set.call(canvas, size)
      void proto
      pin('width'); pin('height')
      if (canvas.style) { canvas.style.width = size + 'px'; canvas.style.height = size + 'px' }
    }
    if (p && typeof p.resize === 'function') p.resize(size, size)
  }, opts.size)

  await page.waitForFunction((size) => {
    const p = window.__noisemakerRenderingPipeline
    if (!p || typeof p.resize !== 'function') return false
    const surf = p.surfaces && p.surfaces.get(p.graph?.renderSurface || 'o0')
    const info = surf && p.backend?.textures?.get(surf.read)
    if (!info) return false
    if (info.width !== size || info.height !== size) {
      p.resize(size, size)
      return false
    }
    return true
  }, opts.size, { timeout: STATUS_TIMEOUT })

  // Round-3 root-cause fix (task-T5-report.md): respawn every stateful
  // surface from a genuine clean slate immediately before the 8-frame
  // protocol, exploiting init.frag's own `pPos.w < 0.5` respawn clause.
  // Verbatim copy of export-and-render.mjs's own block.
  const cleared = await page.evaluate(() => {
    const p = window.__noisemakerRenderingPipeline
    const backend = p?.backend
    const gl = backend?.gl
    if (!p || !gl) return { error: 'no pipeline/gl' }
    const isStateSurface = (name) =>
      name === 'xyz' || name === 'vel' || name === 'rgba' || name === 'trail' ||
      name.endsWith('_xyz') || name.endsWith('_vel') || name.endsWith('_rgba') || name.endsWith('_trail') ||
      /state/i.test(name) || /^(xyz|vel|rgba|points_trail)_node_\d+$/.test(name) ||
      /_pheromone_/.test(name) || /_trail_/.test(name) ||
      /^o\d+$/.test(name) // render/display surfaces (o0-o7) -- final fix wave,
      // see export-and-render.mjs's own copy of this predicate for the full
      // rationale (convolutionFeedback's CHAOS classification was this same
      // bug, just on a surface class this predicate didn't cover yet) and
      // docs/CHAOS-GATE.md. Keep BOTH copies of this predicate in sync --
      // this file's own header comment above calls it a "verbatim copy,"
      // and a full-corpus sweep (parity/sweep.sh) caught the drift the
      // FIRST time this file's copy fell behind (convolutionFeedback FAILed
      // here at the pre-fix mean=10.7 signature while passing an isolated
      // export-and-render.mjs-only check) -- worth grepping for other
      // "verbatim copy" comments in this file before assuming a future
      // export-and-render.mjs change doesn't also need mirroring here.
    const clearedNames = []
    for (const [bareId, surf] of p.surfaces.entries()) {
      if (!isStateSurface(bareId)) continue
      for (const physicalKey of new Set([surf.read, surf.write].filter(Boolean))) {
        const info = backend.textures.get(physicalKey)
        if (!info?.handle) continue
        const fbo = gl.createFramebuffer()
        gl.bindFramebuffer(gl.FRAMEBUFFER, fbo)
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, info.handle, 0)
        if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE) {
          gl.clearColor(0, 0, 0, 0)
          gl.clear(gl.COLOR_BUFFER_BIT)
        }
        gl.bindFramebuffer(gl.FRAMEBUFFER, null)
        gl.deleteFramebuffer(fbo)
      }
      clearedNames.push(bareId)
    }
    gl.finish()
    return { clearedNames }
  })
  if (cleared.error) {
    process.stderr.write(`[batch-golden] WARNING: state-respawn clear reported: ${cleared.error}\n`)
  }

  await page.evaluate(({ time, frames }) => {
    if (window.__noisemakerSetPausedTime) window.__noisemakerSetPausedTime(time)
    const p = window.__noisemakerRenderingPipeline
    const r = window.__noisemakerCanvasRenderer
    for (let i = 0; i < frames; i++) {
      if (p && p.render) p.render(time)
      else if (r && r.render) r.render(time)
    }
  }, { time: opts.time, frames: 8 })

  return capture(page, globals)
}

function parseArgs (argv) {
  const opts = { time: 0.25, size: 256, backend: 'webgl2', chunkSize: 60, list: null }
  const dslPaths = []
  let sawDashDash = false
  const pos = []
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i]
    if (a === '--') { sawDashDash = true; continue }
    if (!sawDashDash && a === '--time') { opts.time = parseFloat(argv[++i]); continue }
    if (!sawDashDash && a === '--size') { opts.size = parseInt(argv[++i], 10); continue }
    if (!sawDashDash && a === '--backend') { opts.backend = argv[++i]; continue }
    if (!sawDashDash && a === '--list') { opts.list = argv[++i]; continue }
    if (!sawDashDash && a === '--chunk-size') { opts.chunkSize = parseInt(argv[++i], 10); continue }
    if (sawDashDash) { dslPaths.push(a); continue }
    pos.push(a)
  }
  opts.outDir = pos[0]
  opts.dslPaths = dslPaths
  return opts
}

function loadList (path) {
  return readFileSync(path, 'utf8').split('\n')
    .map(l => l.trim())
    .filter(l => l && !l.startsWith('#'))
}

async function withSession (opts, fn) {
  process.env.SHADE_VIEWER_ROOT = VIEWER_ROOT
  process.env.SHADE_VIEWER_PATH = VIEWER_PATH
  process.env.SHADE_EFFECTS_DIR = EFFECTS_DIR
  process.env.SHADE_GLOBALS_PREFIX = GLOBALS_PREFIX
  process.env.SHADE_HEADLESS = process.env.SHADE_HEADLESS ?? '1'

  const harness = await import(pathToFileURL(HARNESS).href)
  const { BrowserSession } = harness
  const session = new BrowserSession({ backend: opts.backend })
  await session.setup()
  try {
    const page = session.page
    await session.setBackend(opts.backend)
    const globals = session.globals
    await page.setViewportSize({ width: opts.size, height: opts.size })
    await page.waitForFunction(() => !!window.__noisemakerRenderingPipeline &&
      !!document.getElementById('dsl-editor') && !!document.getElementById('dsl-run-btn'),
    { timeout: STATUS_TIMEOUT })
    return await fn(session, page, globals)
  } finally {
    await session.teardown()
  }
}

async function main () {
  const opts = parseArgs(process.argv.slice(2))
  if (!opts.outDir) {
    process.stderr.write('usage: node batch-golden.mjs <outDir> --list <namesFile> [--size 256] ' +
      '[--time 0.25] [--backend webgl2] [--chunk-size 60] [-- prog1.dsl prog2.dsl ...]\n')
    process.exit(2)
  }
  mkdirSync(opts.outDir, { recursive: true })

  const dslPaths = [...(opts.list ? loadList(opts.list) : []), ...opts.dslPaths]
  if (dslPaths.length === 0) {
    process.stderr.write('[batch-golden] no DSL paths given (--list or trailing args)\n')
    process.exit(2)
  }

  const { exportGraph } = await import(pathToFileURL(EXPORT_GRAPH).href)

  const minted = []
  const failed = []
  const chunkSize = Math.max(1, opts.chunkSize)
  let idx = 0
  const total = dslPaths.length
  const startAll = Date.now()

  for (let chunkStart = 0; chunkStart < dslPaths.length; chunkStart += chunkSize) {
    const chunk = dslPaths.slice(chunkStart, chunkStart + chunkSize)
    process.stderr.write(`[batch-golden] --- chunk ${Math.floor(chunkStart / chunkSize) + 1} ` +
      `(${chunk.length} fixtures, session restart) ---\n`)
    await withSession(opts, async (session, page, globals) => {
      for (const dslPath of chunk) {
        idx++
        const programName = basename(dslPath).replace(/\.dsl$/, '')
        const t0 = Date.now()
        try {
          const dsl = readFileSync(dslPath, 'utf8')
          const graph = await exportGraph(dsl)
          const graphPath = join(opts.outDir, `${programName}.graph.json`)
          writeFileSync(graphPath, JSON.stringify(graph, null, 2) + '\n')

          const pngBuffer = await mintOne(page, globals, opts, dsl, graph.passes?.length, programName)
          const pngPath = join(opts.outDir, `${programName}.golden.png`)
          writeFileSync(pngPath, pngBuffer)

          const consoleErrors = session.getConsoleMessages().map(m => m.text)
          session.clearConsoleMessages()
          const ms = Date.now() - t0
          process.stderr.write(`[batch-golden] (${idx}/${total}) ${programName}: wrote ${pngPath} (${ms}ms)` +
            (consoleErrors.length ? ` [console: ${consoleErrors.join(' | ')}]` : '') + '\n')
          minted.push(programName)
        } catch (err) {
          const ms = Date.now() - t0
          const msg = err?.stack || err?.message || String(err)
          process.stderr.write(`[batch-golden] (${idx}/${total}) ${programName}: FAILED after ${ms}ms: ${msg}\n`)
          failed.push({ programName, error: err?.message || String(err) })
          // A closed/crashed target can't serve the rest of this chunk;
          // surface it so the chunk loop aborts and the NEXT chunk gets a
          // fresh session rather than cascading failures across every
          // remaining fixture in this chunk.
          if (/Target (page|closed)|Target crashed|context or browser has been closed/i.test(msg)) {
            throw err
          }
        }
      }
    }).catch((err) => {
      // Only reached if the inner loop re-threw (fatal target error) or
      // session setup itself failed. Already logged above; move on to the
      // next chunk with a fresh session rather than aborting the whole run.
      process.stderr.write(`[batch-golden] chunk aborted, restarting session: ${err?.message || err}\n`)
    })
  }

  const totalMs = Date.now() - startAll
  process.stderr.write(`[batch-golden] minted=${minted.length} failed=${failed.length} total=${total} ` +
    `(${(totalMs / 1000).toFixed(1)}s)\n`)
  if (failed.length) {
    process.stderr.write(`[batch-golden] FAILED: ${failed.map(f => f.programName).join(' ')}\n`)
  }
  process.stdout.write(`BATCH-GOLDEN: minted=${minted.length} failed=${failed.length} total=${total}\n`)
  process.stdout.write('DONE\n')
  process.exit(failed.length ? 1 : 0)
}

main().catch(err => {
  process.stderr.write(`[batch-golden] FATAL: ${err?.stack || err?.message || JSON.stringify(err)}\n`)
  process.exit(1)
})
