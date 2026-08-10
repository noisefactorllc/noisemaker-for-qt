#!/usr/bin/env node
// export-and-render.mjs — GOLDEN renderer for a parity program.
//
// For one DSL program this:
//   1. exports the normalized render-graph JSON (via tools/export-graph.mjs), and
//   2. renders the reference GPU output to a GOLDEN PNG at a FIXED
//      width/height/seed/frame using the vendored shade-mcp Playwright harness
//      (BrowserSession driving the demo viewer at /demo/shaders/).
//
// The seed is encoded in the DSL itself (e.g. `noise(seed=1)`), so determinism is
// owned by the program. Time is a NORMALIZED 0..1 value (reference/04 §6); we PAUSE
// the demo and pin paused-time, so the capture is a single deterministic frame
// independent of wall clock / FPS.
//
// Default capture: 256x256, time 0.25 (matches scripts/image_regression.py).
//
// We do NOT use the high-level runDslProgram()/renderEffectFrame() helpers because
// neither combines (DSL load) + (paused fixed time) + (deterministic float
// readback of o0). Instead we open a BrowserSession and drive session.page
// directly, reusing the exact viewer hooks those helpers use (DEFAULT viewer
// globals are prefixed __noisemaker — see .mcp.json SHADE_GLOBALS_PREFIX).
//
// Usage:
//   node export-and-render.mjs <program.dsl> <outDir> [--time 0.25] [--size 256] \
//        [--backend webgl2|webgpu]
//
// Writes  <outDir>/<programName>.golden.png  and  <outDir>/<programName>.graph.json
//
// Prereqs: Node, Playwright + a system Chrome (the harness launches chromium),
// and the reference repo at $NM_REFERENCE_ROOT (its shaders/ and demo/ trees).
// See parity/README.md.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve, basename, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const __dirname = dirname(fileURLToPath(import.meta.url))
// Reference (golden) engine path comes from NM_REFERENCE_ROOT. Set
// with NM_REFERENCE_ROOT if it's elsewhere. (This repo was split out of
// a monorepo.)
if (!process.env.NM_REFERENCE_ROOT) {
  throw new Error('NM_REFERENCE_ROOT is not set. Point it at a checkout of the Noisemaker\n'
    + 'reference repo (the JS/WebGL2 engine), e.g. NM_REFERENCE_ROOT=/path/to/noisemaker.\n'
    + 'This repo does NOT assume a sibling checkout.')
}
const REFERENCE_ROOT = resolve(process.env.NM_REFERENCE_ROOT)

const HARNESS = join(REFERENCE_ROOT, 'vendor', 'shade-mcp', 'harness', 'index.js')
const EXPORT_GRAPH = join(__dirname, '..', 'tools', 'export-graph.mjs')

// Self-contained PNG encoder (Node built-in zlib only) so the harness has NO
// external npm dependency (pngjs is a reference devDep that may not be installed).
// Encodes a top-down RGBA8 buffer (row 0 = top) as a non-interlaced PNG.
import { deflateSync } from 'node:zlib'

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

// rgbaTopDown: Uint8 length width*height*4, row 0 = top.
function encodePng (width, height, rgbaTopDown) {
  const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])
  const ihdr = Buffer.alloc(13)
  ihdr.writeUInt32BE(width, 0)
  ihdr.writeUInt32BE(height, 4)
  ihdr[8] = 8   // bit depth
  ihdr[9] = 6   // color type RGBA
  ihdr[10] = 0  // compression
  ihdr[11] = 0  // filter
  ihdr[12] = 0  // interlace
  // Filtered scanlines: each row prefixed with filter byte 0 (None).
  const raw = Buffer.alloc(height * (1 + width * 4))
  for (let y = 0; y < height; y++) {
    const di = y * (1 + width * 4)
    raw[di] = 0
    rgbaTopDown.copy(raw, di + 1, y * width * 4, (y + 1) * width * 4)
  }
  const idat = deflateSync(raw)
  return Buffer.concat([
    sig,
    pngChunk('IHDR', ihdr),
    pngChunk('IDAT', idat),
    pngChunk('IEND', Buffer.alloc(0))
  ])
}

// Viewer wiring — matches .mcp.json (SHADE_VIEWER_ROOT='.', VIEWER_PATH,
// GLOBALS_PREFIX). The harness reads SHADE_* env, so we set them here.
const VIEWER_ROOT = REFERENCE_ROOT
const VIEWER_PATH = '/demo/shaders/'
const EFFECTS_DIR = join(REFERENCE_ROOT, 'shaders', 'effects')
const GLOBALS_PREFIX = '__noisemaker'
const STATUS_TIMEOUT = 300000

// Read back the presented render surface as LINEAR FLOAT, quantize to 8-bit, flip
// to top-down, and encode a PNG buffer. Shared by single-frame + timed-sample modes.
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
  // GL textures are bottom-left origin; flip vertically so the PNG is top-down (the
  // single Y-flip reconciliation point, mirrored by the Qt renderer's own save).
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

// ---- Diagnostic mode: NM_DUMP_INTERMEDIATES ------------------------------
// Round-3 per-frame intermediate-state bisection (task-T5, physarum agent-
// state parity investigation). Reads back named `pipeline.surfaces` bare-name
// entries' CURRENT read-side physical texture straight from the page's own
// WebGL2Backend registry (`pipeline.backend.textures`), the exact same
// lookup `capture()` above already does for the final render surface — this
// just generalizes it to arbitrary intermediate/state surfaces and writes
// each as a raw float32 .bin (LE, RGBA-interleaved, bottom-up GL row order,
// NOT flipped -- the Qt-side dumper this compares against also skips the
// flip, since this diagnostic compares GPU buffer contents directly, not
// display-oriented images). Execute-only against the reference: this reads
// live page JS state via Playwright's ordinary page.evaluate() (the same
// mechanism `capture()`/the render-loop already use throughout this file);
// no reference-repo file is opened, read from disk, or written.
//
// Env: NM_DUMP_INTERMEDIATES=1 to enable. NM_DUMP_SURFACES="bare,names,here"
// to override the default surface list (comma-separated bare texId names,
// matching `pipeline.surfaces` keys -- i.e. WITHOUT the `global_` prefix).
// Default targets physarumNoSense's own surfaces (see task-T5-report.md
// round-3 section for why these four): the pointsEmit/physarum agent-state
// MRT group and both trail accumulators.
const DEFAULT_DUMP_SURFACES = [
  'xyz_node_1', 'vel_node_1', 'rgba_node_1',
  'physarum_pheromone_chain_0', 'points_trail_node_1'
]

function dumpSurfaceList () {
  const raw = process.env.NM_DUMP_SURFACES
  if (!raw) return DEFAULT_DUMP_SURFACES
  return raw.split(',').map(s => s.trim()).filter(Boolean)
}

// Reads back each named bare surface's current READ-side physical texture.
// Returns { [bareId]: { width, height, base64 } | { error, availableSurfaceKeys, availableTextureKeys } }.
async function dumpIntermediates (page, globals, bareIds) {
  return page.evaluate(({ g, bareIds }) => {
    // Chunked Uint8Array->base64 (avoids String.fromCharCode.apply stack
    // limits on the ~256KB+ buffers a 256x256 rgba32f surface produces).
    function toBase64 (typedArray) {
      const bytes = new Uint8Array(typedArray.buffer, typedArray.byteOffset, typedArray.byteLength)
      let binary = ''
      const chunk = 8192
      for (let i = 0; i < bytes.length; i += chunk) {
        binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk))
      }
      return btoa(binary)
    }
    const pipeline = window[g.renderingPipeline]
    const out = {}
    if (!pipeline) { out.__error = 'no pipeline'; return out }
    const backend = pipeline.backend
    const gl = backend?.gl
    if (!gl) { out.__error = 'no gl'; return out }
    for (const bareId of bareIds) {
      const surf = pipeline.surfaces?.get(bareId)
      const physicalKey = surf ? surf.read : bareId
      const info = backend.textures?.get(physicalKey)
      if (!info?.handle) {
        out[bareId] = {
          error: `no texture at physicalKey=${physicalKey} (surf=${surf ? JSON.stringify(surf) : 'undefined'})`,
          availableSurfaceKeys: Array.from(pipeline.surfaces?.keys() || []),
          availableTextureKeys: Array.from(backend.textures?.keys() || [])
        }
        continue
      }
      const { handle, width, height, glFormat } = info
      const fbo = gl.createFramebuffer()
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo)
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, handle, 0)
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
        gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.deleteFramebuffer(fbo)
        out[bareId] = { error: 'FBO incomplete', physicalKey }
        continue
      }
      // Match capture()'s own isFloat test: rgba8 surfaces (e.g. this
      // graph's rgba_node_1) must be read as UNSIGNED_BYTE -- readPixels
      // with FLOAT against an 8-bit-normalized framebuffer is an ANGLE
      // GL_INVALID_OPERATION (silently leaves the buffer unwritten, not a
      // JS exception), caught empirically on the first run of this probe.
      const isFloat = glFormat?.type === gl.HALF_FLOAT || glFormat?.type === gl.FLOAT
      gl.finish()
      let dtype, base64
      if (isFloat) {
        const buf = new Float32Array(width * height * 4)
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.FLOAT, buf)
        dtype = 'f32'
        base64 = toBase64(buf)
      } else {
        const buf = new Uint8Array(width * height * 4)
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, buf)
        dtype = 'u8'
        base64 = toBase64(buf)
      }
      gl.bindFramebuffer(gl.FRAMEBUFFER, null)
      gl.deleteFramebuffer(fbo)
      out[bareId] = { width, height, physicalKey, dtype, base64 }
    }
    return out
  }, { g: globals, bareIds })
}

function writeIntermediateDump (outDir, programName, frameIndex, bareId, entry) {
  if (entry.error) {
    process.stderr.write(`[introspect] ${programName} frame${frameIndex} ${bareId}: ${entry.error}\n` +
      `  availableSurfaceKeys=${JSON.stringify(entry.availableSurfaceKeys || [])}\n` +
      `  availableTextureKeys=${JSON.stringify(entry.availableTextureKeys || [])}\n`)
    return
  }
  const { width, height, dtype, base64 } = entry
  const buf = Buffer.from(base64, 'base64')
  const path = join(outDir, `${programName}.frame${frameIndex}.${bareId}.w${width}.h${height}.${dtype}.bin`)
  writeFileSync(path, buf)
  process.stderr.write(`[introspect] wrote ${path} (${buf.length} bytes)\n`)
}

function parseArgs (argv) {
  const opts = { time: 0.25, size: 256, backend: 'webgl2', runSeconds: 0, sampleEvery: 5 }
  const pos = []
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i]
    if (a === '--time') opts.time = parseFloat(argv[++i])
    else if (a === '--size') opts.size = parseInt(argv[++i], 10)
    else if (a === '--backend') opts.backend = argv[++i]
    else if (a === '--run-seconds') opts.runSeconds = parseInt(argv[++i], 10)
    else if (a === '--sample-every') opts.sampleEvery = parseInt(argv[++i], 10)
    else pos.push(a)
  }
  opts.programPath = pos[0]
  opts.outDir = pos[1]
  return opts
}

// Decode the demo's data:image/png;base64 URI to a Buffer.
function dataUriToBuffer (uri) {
  const comma = uri.indexOf(',')
  return Buffer.from(uri.slice(comma + 1), 'base64')
}

async function main () {
  const opts = parseArgs(process.argv.slice(2))
  if (!opts.programPath || !opts.outDir) {
    process.stderr.write('usage: node export-and-render.mjs <program.dsl> <outDir> ' +
      '[--time 0.25] [--size 256] [--backend webgl2|webgpu]\n')
    process.exit(2)
  }

  const dsl = readFileSync(opts.programPath, 'utf8')
  const programName = basename(opts.programPath).replace(/\.dsl$/, '')
  mkdirSync(opts.outDir, { recursive: true })

  // ---- 1. Export the normalized graph JSON (no browser needed) -------------
  const { exportGraph } = await import(pathToFileURL(EXPORT_GRAPH).href)
  const graph = await exportGraph(dsl)
  const graphPath = join(opts.outDir, `${programName}.graph.json`)
  writeFileSync(graphPath, JSON.stringify(graph, null, 2) + '\n')
  process.stderr.write(`[parity] wrote ${graphPath}\n`)

  // ---- 2. Render the golden frame via the Playwright harness ---------------
  // Configure the harness via SHADE_* env (read by BrowserSession.getConfig()).
  process.env.SHADE_VIEWER_ROOT = VIEWER_ROOT
  process.env.SHADE_VIEWER_PATH = VIEWER_PATH
  process.env.SHADE_EFFECTS_DIR = EFFECTS_DIR
  process.env.SHADE_GLOBALS_PREFIX = GLOBALS_PREFIX
  process.env.SHADE_HEADLESS = process.env.SHADE_HEADLESS ?? '1'

  const harness = await import(pathToFileURL(HARNESS).href)
  const { BrowserSession } = harness

  const session = new BrowserSession({ backend: opts.backend })
  let pngBuffer
  let sampled = false
  try {
    await session.setup()
    const page = session.page
    await session.setBackend(opts.backend)
    const globals = session.globals

    await page.setViewportSize({ width: opts.size, height: opts.size })

    // Wait for the demo's initial pipeline + DSL editor to be ready (the demo
    // boots with a default effect; __noisemakerRenderingPipeline is set then).
    await page.waitForFunction(() => !!window.__noisemakerRenderingPipeline &&
      !!document.getElementById('dsl-editor') && !!document.getElementById('dsl-run-btn'),
    { timeout: STATUS_TIMEOUT })

    // Load OUR DSL via the editor + run button, then wait for the pipeline's
    // graph to actually SWAP to our program (graph.id change). Polling only the
    // status text races the default-effect "compiled" message and reads the
    // wrong surface (the bug that produced identical default goldens).
    const baselineId = await page.evaluate(() =>
      window.__noisemakerRenderingPipeline?.graph?.id ?? null)
    await page.evaluate((src) => {
      const editor = document.getElementById('dsl-editor')
      const runBtn = document.getElementById('dsl-run-btn')
      editor.value = src
      editor.dispatchEvent(new Event('input', { bubbles: true }))
      runBtn.click()
    }, dsl)
    await page.waitForFunction((base) => {
      const s = (document.getElementById('status')?.textContent || '').toLowerCase()
      if (s.includes('error') || s.includes('failed')) {
        throw new Error('DSL compile failed: ' + document.getElementById('status')?.textContent)
      }
      const p = window.__noisemakerRenderingPipeline
      return !!(p && p.graph && p.graph.id !== base)
    }, { timeout: STATUS_TIMEOUT }, baselineId)

    // PAUSE FIRST so the demo's requestAnimationFrame loop stops re-syncing the
    // canvas to its (small, letterboxed) layout size — that auto-resize is what
    // intermittently reverted our resize to ~90px.
    await page.evaluate(() => {
      if (window.__noisemakerSetPaused) window.__noisemakerSetPaused(true)
    })
    // Resize the render surface to the requested square (canvas backing + CSS + the
    // pipeline's own surfaces) so the readback is deterministic and matches the Qt
    // renderer.
    //
    // The demo recomputes a (small, letterboxed) square canvas size from the
    // container layout inside a `resize`-event handler (demo/shaders/index.html
    // computeCanvasSize/handleResize). page.setViewportSize() dispatches that
    // `resize` event asynchronously, so its handler can fire AFTER this block and
    // revert canvas.width/height (and thus the pipeline surfaces) back to ~90px —
    // a race that intermittently produced 90x90 goldens. To make the resize
    // deterministic we PIN the canvas width/height setters to our target before
    // resizing, so any late layout-driven write is a no-op, then we poll until the
    // o0 surface texture is stably at the requested size before rendering.
    await page.evaluate((size) => {
      const r = window.__noisemakerCanvasRenderer
      const p = window.__noisemakerRenderingPipeline
      const canvas = r && r.canvas
      if (canvas) {
        // Pin width/height: the real backing store is set to `size`; any later
        // assignment (e.g. the demo's handleResize) is swallowed. configurable so
        // this is reversible and overrides the renderer's own interceptor.
        const pin = (prop) => {
          Object.defineProperty(canvas, prop, {
            configurable: true,
            enumerable: true,
            get () { return size },
            set () { /* locked to `size` for deterministic capture */ }
          })
        }
        // Set the true backing store first via the prototype setter, then lock.
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

    // Poll until the presented o0 surface texture is stably at the requested size.
    // This drains any pending layout `resize` event and re-asserts the pipeline
    // size, guaranteeing the readback below sees a `size`x`size` surface.
    // NOTE: Playwright's signature is waitForFunction(fn, arg, options) — the
    // single page-function ARG comes BEFORE the options object.
    await page.waitForFunction((size) => {
      const p = window.__noisemakerRenderingPipeline
      if (!p || typeof p.resize !== 'function') return false
      const surf = p.surfaces && p.surfaces.get(p.graph?.renderSurface || 'o0')
      const info = surf && p.backend?.textures?.get(surf.read)
      if (!info) return false
      if (info.width !== size || info.height !== size) {
        // Re-assert (cheap no-op when already correct) and keep waiting.
        p.resize(size, size)
        return false
      }
      return true
    }, opts.size, { timeout: STATUS_TIMEOUT })

    if (opts.runSeconds === 0) {
      // ROOT-CAUSE FIX (round 3, task-T5 physarum agent-state parity
      // investigation; mechanism corrected round 4 -- see below): the demo's
      // CanvasRenderer runs an always-on requestAnimationFrame loop
      // (shaders/src/renderer/canvas.js _renderLoop/start()/stop()), gated
      // ONLY on `this._isRunning` -- NOT on compile state. `compile()` (the
      // DSL-swap path this harness drives via the run button) sets
      // `pipeline.isCompiling = true` before recompiling and clears it when
      // `compilePrograms()` finishes, but never calls `stop()` in the normal
      // (non-context-loss) path -- `start()` is only reached conditionally,
      // deep in a context-loss-recovery branch. So the loop, once started at
      // page boot, keeps firing every animation frame straight through a DSL
      // swap: `pipeline.render()` (shaders/src/runtime/pipeline.js:1285)
      // itself checks `isCompiling` and no-ops WHILE the graph is actually
      // compiling, but the instant that flag clears, the still-running,
      // never-stopped loop's very next tick -- and every tick after it --
      // is a REAL render() call, completely independent of anything UI-
      // related. It keeps free-running on wall-clock time until this
      // script's own `__noisemakerSetPaused(true)` call (below) actually
      // executes in the page and reaches `renderer.stop()`, which is subject
      // to real Playwright/CDP round-trip latency from when Node issues it
      // to when the page-side JS runs -- an inherently load-dependent
      // window, exactly matching the observed spread (1, 26, 27, 28 extra
      // renders measured across otherwise-identical runs; a fixed, graph-
      // structure-driven count would not vary like that). The reference's
      // own deterministic tests sidestep this entirely by construction --
      // e.g. shaders/tests/test_spawnpoint_wgsl_vertical_mirror.mjs creates
      // a CanvasRenderer and drives it purely via direct `renderer.render(t)`
      // calls, and never calls `renderer.start()` at all, so the RAF loop
      // never exists to race against. (Round 3 originally attributed this to
      // demo/shaders/index.html's onControlChange-driven
      // renderSingleFrameIfPaused(); that call site is real but is gated on
      // `isPaused`, which doesn't explain the timing here -- corrected after
      // re-review traced the actual mechanism to the RAF loop above.) Not
      // reference-engine chaos, not a downstream-port bug -- a genuine race
      // in THIS file's own capture protocol. `resize()` does not clear it
      // (`createSurfaces()` short-circuits when dimensions are already
      // correct; confirmed empirically, including forcing a real 1x1->size
      // dimension churn to defeat that short-circuit) -- the fix has to
      // reach past whatever caused the extra renders and re-establish a
      // known-clean state directly, which is what it does below regardless
      // of the exact upstream cause.
      //
      // Fix: exploit init.frag's OWN existing respawn contract directly --
      // `needsRespawn = resetState || (pPos.w < 0.5) || (time<0.01 &&
      // pPos.w==0.0)` -- the `pPos.w < 0.5` ("dead/uninitialized") clause
      // does not depend on `time` or `resetState` at all. Clearing every
      // stateful surface's CURRENT physical texture(s) to (0,0,0,0) makes
      // every agent's `alive` flag 0, so the very next render() call
      // unconditionally respawns every agent from a genuine clean slate --
      // the same mechanism the shader already uses for ordinary agent death/
      // respawn, not a harness-specific hack. Scoped to `opts.runSeconds ===
      // 0` (the discrete-8-frame protocol) only -- NOT applied before the
      // timed-sampling branch above, whose continuously-evolving fluid/
      // feedback sims (navierStokes etc.) have no "respawn from zero"
      // semantics and were not part of this investigation. Verified fix:
      // re-minted physarumNoSense golden with this in place matches the
      // already-independently-verified-correct Qt candidate bit-for-bit at
      // all 8 frames (see task-T5-report.md round-3 section).
      const cleared = await page.evaluate(() => {
        const p = window.__noisemakerRenderingPipeline
        const backend = p?.backend
        const gl = backend?.gl
        if (!p || !gl) return { error: 'no pipeline/gl' }
        // NOTE (round 4 re-review): this predicate is a deliberate SUPERSET
        // of the reference's own canonical isStateSurface/_is_state_surface
        // (pipeline.js swapBuffers() / the godot and Qt ports' matching
        // heuristics), which this harness fix does not need to match
        // exactly -- the two extra patterns (`/state/i` beyond the reference's
        // exact xyz/vel/rgba/trail family, and `/_pheromone_/`/`/_trail_/`
        // as substring matches rather than the reference's stricter suffix/
        // regex forms) intentionally clear MORE surfaces than the reference
        // would classify as "state," which is safe and correct for this
        // fix's actual job (force a full, clean sim reset before the 8-frame
        // protocol -- clearing a non-state surface here is a harmless no-op
        // for anything downstream, since nothing else in this file reads a
        // surface's PRE-reset content). It would NOT be safe to reuse this
        // broadened predicate anywhere that needs to match the reference's
        // real ping-pong/state semantics exactly (e.g. a backend's own
        // hazard-surface classification) -- and if a future surface ever
        // matches this predicate WITHOUT having an init-shader "alive/dead"
        // respawn contract like this graph family's `pPos.w < 0.5` (some
        // other effect's own state convention, or a surface this predicate
        // over-matches by name alone), clearing it to (0,0,0,0) here would
        // just leave it zeroed with no shader-side respawn to repopulate it
        // -- worth a second look before extending this fix to a new fixture
        // family, not assumed to generalize for free.
        //
        // EXTENSION (final fix wave, item 7 / CHAOS-GATE investigation):
        // `o0`-`o7` (render/display surfaces) added after finding
        // `convolutionFeedback` -- classified CHAOS ("reference-side
        // non-determinism") on the strength of exactly the same symptom
        // physarum had before the T5-round-3 fix above -- reads its OWN
        // prior-frame `global_o0` back as `feedbackTex` input (`filter/
        // convolutionFeedback` sharpen->blur->blend chain), a genuine
        // frame-to-frame hazard surface that just isn't agent-STATE-shaped,
        // so the original predicate never covered it. Verified directly:
        // clearing o0-o7 here makes `convolutionFeedback`'s golden bit-exact
        // reproducible across independent mints (was max-abs-diff=175,
        // mean=38.9, 65535/65536 px differing between two back-to-back
        // mints; is now max-diff=0) -- this was the SAME RAF-loop-race
        // harness bug throughout, not inherent reference chaos. Reclassified
        // out of sweep.sh's is_chaos() into an ordinary tol_for() NEAR entry
        // as a result -- see docs/CHAOS-GATE.md (rewritten for this finding)
        // and parity/sweep.sh's tol_for()/is_chaos() for the follow-through.
        // A render surface a graph never reads back (the common case: o0
        // written once, presented, never sampled by an earlier pass in a
        // LATER frame) is unaffected by being clearable here -- clearing it
        // pre-protocol is a no-op for any graph that doesn't feed it back
        // into itself the way convolutionFeedback does.
        const isStateSurface = (name) =>
          name === 'xyz' || name === 'vel' || name === 'rgba' || name === 'trail' ||
          name.endsWith('_xyz') || name.endsWith('_vel') || name.endsWith('_rgba') || name.endsWith('_trail') ||
          /state/i.test(name) || /^(xyz|vel|rgba|points_trail)_node_\d+$/.test(name) ||
          /_pheromone_/.test(name) || /_trail_/.test(name) ||
          /^o\d+$/.test(name) // render/display surfaces (o0-o7) -- see below
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
      if (cleared.clearedNames?.length) {
        process.stderr.write(`[parity] reset stateful surfaces before the 8-frame protocol: ${JSON.stringify(cleared.clearedNames)}\n`)
      }
    }

    // ---- 3. Render + capture: one pinned frame, or timed samples for statefuls.
    if (opts.runSeconds > 0) {
      // Timed-sampling mode (fluid/feedback sims): step total_frames at 1/600
      // normalized dt (one 60fps frame in the 10s loop), driving pipeline.render(t)
      // DIRECTLY (NO setPausedTime — let lastTime advance so deltaTime>0 and the sim
      // EVOLVES), capturing every sample_every seconds to <name>.golden.t<sec>.png.
      const everyFrames = opts.sampleEvery * 60
      const totalFrames = opts.runSeconds * 60
      const numSamples = Math.max(1, Math.floor(totalFrames / everyFrames))
      for (let s = 0; s < numSamples; s++) {
        await page.evaluate(({ everyFrames, startFrame }) => {
          const p = window.__noisemakerRenderingPipeline
          for (let i = 0; i < everyFrames; i++) {
            const t = ((startFrame + i + 1) / 600) % 1.0
            if (p && p.render) p.render(t)
          }
        }, { everyFrames, startFrame: s * everyFrames })
        const buf = await capture(page, globals)
        const sec = (s + 1) * opts.sampleEvery
        const sp = join(opts.outDir, `${programName}.golden.t${sec}.png`)
        writeFileSync(sp, buf)
        process.stderr.write(`[parity] wrote ${sp}\n`)
      }
      sampled = true
    } else if (process.env.NM_DUMP_INTERMEDIATES) {
      // Round-3 diagnostic: same 8-frame protocol as the plain branch below,
      // but driven one render() call at a time from Node so we can read back
      // intermediate surfaces after each frame. Same pause/pin-time setup
      // already done above; identical `p.render(time)` call per iteration.
      await page.evaluate(({ time }) => {
        if (window.__noisemakerSetPausedTime) window.__noisemakerSetPausedTime(time)
      }, { time: opts.time })
      const bareIds = dumpSurfaceList()
      const dumpOutDir = join(opts.outDir, 'introspect', 'golden')
      mkdirSync(dumpOutDir, { recursive: true })
      for (let i = 0; i < 8; i++) {
        await page.evaluate(({ time }) => {
          const p = window.__noisemakerRenderingPipeline
          const r = window.__noisemakerCanvasRenderer
          if (p && p.render) p.render(time)
          else if (r && r.render) r.render(time)
        }, { time: opts.time })
        const dump = await dumpIntermediates(page, globals, bareIds)
        if (dump.__error) {
          process.stderr.write(`[introspect] frame ${i}: ${dump.__error}\n`)
          continue
        }
        for (const bareId of bareIds) {
          writeIntermediateDump(dumpOutDir, programName, i, bareId, dump[bareId])
        }
      }
      pngBuffer = await capture(page, globals)
    } else {
      // Pin the normalized frame time, then render 8 deterministic frames by driving
      // the PIPELINE directly (the CanvasRenderer re-syncs canvas size per frame and
      // can revert the resize; pipeline.render does the GPU work the readback reads).
      await page.evaluate(({ time, frames }) => {
        if (window.__noisemakerSetPausedTime) window.__noisemakerSetPausedTime(time)
        const p = window.__noisemakerRenderingPipeline
        const r = window.__noisemakerCanvasRenderer
        for (let i = 0; i < frames; i++) {
          if (p && p.render) p.render(time)
          else if (r && r.render) r.render(time)
        }
      }, { time: opts.time, frames: 8 })
      pngBuffer = await capture(page, globals)
    }

    const consoleErrors = session.getConsoleMessages().map(m => m.text)
    if (consoleErrors.length) {
      process.stderr.write(`[parity] console messages during render:\n  ${consoleErrors.join('\n  ')}\n`)
    }
  } finally {
    await session.teardown()
  }

  if (!sampled) {
    const pngPath = join(opts.outDir, `${programName}.golden.png`)
    writeFileSync(pngPath, pngBuffer)
    process.stderr.write(`[parity] wrote ${pngPath} (${opts.size}x${opts.size}, time=${opts.time}, backend=${opts.backend})\n`)
  }
}

main().catch(err => {
  process.stderr.write(`[parity] FAILED: ${err?.stack || err?.message || JSON.stringify(err)}\n`)
  process.exit(1)
})
