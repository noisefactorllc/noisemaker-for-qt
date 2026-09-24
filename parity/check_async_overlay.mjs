// check_async_overlay.mjs -- asyncInit overlay parity gate (GAP-025).
//
// Oracle: the reference asyncInit of filter/fibers, filter/scratches and
// filter/strayHair (the unchanged definition.js modules and
// shaders/src/cpu/wormTracer.js) run in headless Chromium, launched the way
// the golden minter launches it (vendor/shade-mcp BrowserSession: headless,
// --disable-gpu-sandbox, --use-angle=metal on macOS). The canvas is read
// back through WebGL texImage2D: with UNPACK_PREMULTIPLY_ALPHA_WEBGL true
// for the premultiplied backing store, and false for the bytes the
// reference pipeline uploads as overlayTex.
// Candidate: qt/build/tests/async_overlay_dump (runtime/async_overlay.h,
// stroke_canvas.h, worm_tracer.h).
//
// Sections and acceptance:
//   math       Math.sin, Math.cos, Math.log on 300000 random arguments:
//              every result bit-identical.
//   upload     every premultiplied (c, a) pair, c <= a, a > 0: the upload
//              conversion bit-identical.
//   strokes    seeded stroke lists drawn on both canvases, and
//   overlay    the asyncInit overlays: premultiplied canvas bytes differ by
//              at most 1, in at most 1 of 5000 channel values; the uploaded
//              bytes differ only at pixels whose canvas bytes differ.
// The residual is GPU arithmetic: at a pixel whose blended value lies
// within a few float ulps of a rounding midpoint, the Metal shader's
// rsqrt, division and fused operations can round the other way. Measured
// on macOS arm64 (Apple M4), Chromium 153, 2026-09-24: fibers 256x256
// 14 of 262144 canvas channel values differ by 1; scratches and strayHair
// at 256x256 are identical.
//
// The oracle is Chromium's GPU canvas (Skia Graphite on Metal); another GPU
// backend rasterizes strokes differently, so the gate refuses to run on a
// renderer other than ANGLE Metal (exit 3). Measured on Linux arm64, Chromium
// 153 with --use-gl=angle --use-angle=gl over Mesa llvmpipe: Skia Ganesh on
// GL, the same 51872 traced strokes bit for bit, and a different canvas
// (fibers 90246, scratches 16492, strayHair 134 of 262144 values differ).
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/check_async_overlay.mjs
// Env: NM_ASYNC_OVERLAY_DUMP  candidate binary (default qt/build/tests/async_overlay_dump)

import { resolve, dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { existsSync, mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const DUMP = process.env.NM_ASYNC_OVERLAY_DUMP || join(REPO, 'qt', 'build', 'tests', 'async_overlay_dump')
const MAX_DIFF = 1
const MAX_DIFF_FRACTION = 1 / 5000

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }
if (!existsSync(DUMP)) { console.error(`async_overlay_dump not found at ${DUMP} (build qt/build first)`); process.exit(3) }
const REF = resolve(process.env.NM_REFERENCE_ROOT)
const { chromium } = await import(pathToFileURL(join(REF, 'node_modules', 'playwright', 'index.mjs')).href)
const harness = await import(pathToFileURL(join(REF, 'vendor', 'shade-mcp', 'harness', 'index.js')).href)

// The port restates these reference statements; stop if the reference changed them.
const restated = {
    'shaders/src/cpu/wormTracer.js': [
        'this.state = ((seed >>> 0) * 747796405 + 2891336453) >>> 0',
        'const word = (((this.state >>> ((this.state >>> 28) + 4)) ^ this.state) * 277803737) >>> 0',
        'return mean + std * Math.sqrt(-2 * Math.log(u1)) * Math.cos(TAU * u2)',
        'const flowField = valueNoiseField(width, height, flowFreq, new SeededRNG(seed * 31337))',
        'const count = Math.max(1, Math.floor(maxDim * density))',
        'const iterations = Math.max(1, Math.floor(Math.sqrt(minDim) * duration))',
        "ctx.lineCap = 'round'",
        'let angle = fieldVal * TAU * kink',
        'ctx.strokeStyle = `rgba(${r}, ${g}, ${b}, ${a * exposure})`',
        'if (w % 3 === 0) {',
    ],
    'shaders/effects/filter/fibers/definition.js': [
        'const baseDensity = 0.5 + density * 2.0',
        'const layerSeed = seed * 1000 + layer * 137',
        'lineWidth: Math.max(1.5, width / 384)',
    ],
    'shaders/effects/filter/scratches/definition.js': [
        'const layerSeed = seed * 1000 + layer * 251',
        'density: 0.1 + density * 0.4',
        'lineWidth: Math.max(0.5, width / 1024)',
    ],
    'shaders/effects/filter/strayHair/definition.js': [
        'const layerSeed = seed * 1000 + 42',
        'density: 0.001 + density * 0.004',
        'lineWidth: Math.max(1, width / 400)',
    ],
    'shaders/src/runtime/pipeline.js': [
        "this.backend.updateTextureFromSource(texId, canvas, { flipY: true })",
        'params: params ? { ...params } : { ...this.globalUniforms },',
    ],
}
for (const [file, statements] of Object.entries(restated)) {
    const text = readFileSync(join(REF, file), 'utf8')
    for (const statement of statements) {
        if (!text.includes(statement)) {
            console.error(`FAIL reference ${file} no longer contains: ${statement}`)
            process.exit(1)
        }
    }
}

// ---------------------------------------------------------------- cases

// Deterministic generator for the stroke scenes (not the reference RNG).
function lcg (seed) {
    let s = seed >>> 0
    return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 }
}

function strokeScene (name, width, height, seed, build) {
    const r = lcg(seed)
    const records = []
    build(r, (x0, y0, x1, y1, lineWidth, alpha, cr, cg, cb) => records.push(x0, y0, x1, y1, lineWidth, alpha, cr, cg, cb))
    return { name, width, height, records }
}

const strokeScenes = [
    // Isolated segments: widths from subpixel to 2.3 px, lengths 0..5 px.
    strokeScene('isolated', 512, 512, 7, (r, add) => {
        const widths = [1.5, 1.0, 0.5, 2.3]
        for (let cy = 0; cy < 32; cy++) {
            for (let cx = 0; cx < 32; cx++) {
                const x0 = cx * 16 + 5 + r() * 4
                const y0 = cy * 16 + 5 + r() * 4
                const len = r() < 0.5 ? r() * 0.4 : r() * 5
                const angle = r() * Math.PI * 2
                add(x0, y0, x0 + Math.sin(angle) * len, y0 + Math.cos(angle) * len, widths[(cy * 32 + cx) % 4], 1, 255, 255, 255)
            }
        }
    }),
    // Overlapping translucent chains, the shape of a traced worm.
    strokeScene('chains', 512, 512, 22, (r, add) => {
        const widths = [1.5, 1.0, 0.5, 2.3]
        for (let i = 0; i < 1024; i++) {
            let x = r() * 512
            let y = r() * 512
            let angle = r() * Math.PI * 2
            const w = widths[i % 4]
            const c = [Math.floor(r() * 256), Math.floor(r() * 256), Math.floor(r() * 256)]
            const a0 = r()
            for (let k = 0; k < 12; k++) {
                const len = r() < 0.5 ? r() * 0.4 : r() * 3
                angle += r() - 0.5
                const nx = x + Math.sin(angle) * len
                const ny = y + Math.cos(angle) * len
                add(x, y, nx, ny, w, a0 * (1 - Math.abs(1 - 2 * k / 11)), c[0], c[1], c[2])
                x = nx
                y = ny
            }
        }
    }),
    // Segments of zero length, of zero length once converted to float,
    // one float ulp long, and a little longer, at widths 3 and 1.
    strokeScene('degenerate', 104, 32, 1, (r, add) => {
        const bits = new Uint32Array(1)
        const asFloat = new Float32Array(bits.buffer)
        const nextFloatUp = (x) => { asFloat[0] = Math.fround(x); bits[0] += 1; return asFloat[0] }
        for (const [row, width] of [[8.4, 3], [24.4, 1]]) {
            add(8.3, row, 8.3, row, width, 1, 255, 255, 255)
            add(24.3, row, 24.3 + 1e-9, row, width, 1, 255, 255, 255)
            add(Math.fround(40.3), row, nextFloatUp(40.3), row, width, 1, 255, 255, 255)
            add(56.3, row, 56.3 + 1e-5, row + 1e-5, width, 1, 255, 255, 255)
            add(72.3, row, 72.3 + 0.001, row, width, 1, 255, 255, 255)
            add(88.3, row, 88.3 + 0.01, row, width, 1, 255, 255, 255)
        }
    }),
    // Segments across and just outside the edges of a non-square canvas.
    strokeScene('border', 96, 64, 5, (r, add) => {
        for (let i = 0; i < 400; i++) {
            const side = i % 4
            const t = r()
            const off = (r() - 0.5) * 4
            const x0 = side === 0 ? off : side === 1 ? 96 + off : t * 96
            const y0 = side === 2 ? off : side === 3 ? 64 + off : t * 64
            const angle = r() * Math.PI * 2
            const len = r() * 2
            add(x0, y0, x0 + Math.sin(angle) * len, y0 + Math.cos(angle) * len, [0.5, 1, 1.5, 5][i % 4], 0.25 + 0.75 * r(), 255, 128, 64)
        }
    }),
]

const overlayCases = [
    { name: 'fibers-256', effect: 'filter/fibers', width: 256, height: 256, params: { density: 1, seed: 1, alpha: 0.5 } },
    { name: 'scratches-256', effect: 'filter/scratches', width: 256, height: 256, params: { density: 0.3, seed: 1, alpha: 0.75 } },
    { name: 'strayHair-256', effect: 'filter/strayHair', width: 256, height: 256, params: { density: 0.5, seed: 1, alpha: 0.5 } },
    { name: 'fibers-333x197', effect: 'filter/fibers', width: 333, height: 197, params: { density: 0.3, seed: 7 } },
    { name: 'scratches-512x288', effect: 'filter/scratches', width: 512, height: 288, params: { seed: 3 } },
    { name: 'strayHair-301x401', effect: 'filter/strayHair', width: 301, height: 401, params: { density: 1, seed: 5 } },
    { name: 'fibers-defaults-128', effect: 'filter/fibers', width: 128, height: 128, params: {} },
]

// ---------------------------------------------------------------- oracle

const baseUrl = await harness.acquireServer(0, REF, join(REF, 'shaders', 'effects'))
const launchArgs = ['--disable-gpu-sandbox']
if (process.platform === 'darwin') launchArgs.push('--use-angle=metal')
const browser = await chromium.launch({ headless: true, args: launchArgs })
let oracle
try {
    const page = await browser.newPage()
    await page.goto(`${baseUrl}/index.html`).catch(() => {})
    await page.setContent('<!doctype html><title>async overlay oracle</title>')
    oracle = await page.evaluate(async ({ baseUrl, strokeScenes, overlayCases }) => {
        const b64 = (u8) => {
            let s = ''
            for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode(...u8.subarray(i, i + 0x8000))
            return btoa(s)
        }
        const gl = document.createElement('canvas').getContext('webgl2')
        const debug = gl.getExtension('WEBGL_debug_renderer_info')
        const renderer = debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER)
        // Upload `canvas` as WebGL does and read the texture back, row 0 = canvas top.
        const readBack = (canvas, premultiply) => {
            const tex = gl.createTexture()
            gl.bindTexture(gl.TEXTURE_2D, tex)
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false)
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, premultiply)
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, canvas)
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false)
            const fbo = gl.createFramebuffer()
            gl.bindFramebuffer(gl.FRAMEBUFFER, fbo)
            gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0)
            const out = new Uint8Array(canvas.width * canvas.height * 4)
            gl.readPixels(0, 0, canvas.width, canvas.height, gl.RGBA, gl.UNSIGNED_BYTE, out)
            gl.bindFramebuffer(gl.FRAMEBUFFER, null)
            gl.deleteFramebuffer(fbo)
            gl.deleteTexture(tex)
            return out
        }
        const result = { renderer, math: null, upload: null, strokes: [], overlays: [] }

        // math: random doubles from random bits.
        let seed = 12345
        const next = () => { seed = (Math.imul(seed, 1103515245) + 12345) >>> 0; return seed }
        const view = new DataView(new ArrayBuffer(8))
        const unit = () => { view.setUint32(0, 0x3FF00000 | (next() >>> 12)); view.setUint32(4, next()); return view.getFloat64(0) - 1 }
        const math = []
        for (let i = 0; i < 100000; i++) {
            const u = unit()
            const m = i % 5
            const x = m === 0 ? u * 6.283185307179586 : m === 1 ? u * 700 - 10 : m === 2 ? (u - 0.5) * 20
                : m === 3 ? u * 1e5 : (u - 0.5) * 1e-3
            const y = Math.max(unit(), 1e-10)
            math.push(0, x, Math.sin(x), 1, x, Math.cos(x), 2, y, Math.log(y))
        }
        result.math = b64(new Uint8Array(new Float64Array(math).buffer))

        // upload: one pixel per premultiplied (c, a) pair, drawn as an
        // opaque-channel fill whose 8-bit premultiplication gives c.
        const pairs = []
        for (let a = 1; a < 256; a++) {
            const red = new Map()
            for (let R = 0; R < 256; R++) { const c = Math.floor(R * a / 255 + 0.5); if (!red.has(c)) red.set(c, R) }
            for (let c = 0; c <= a; c++) pairs.push([red.get(c), a])
        }
        const up = document.createElement('canvas')
        up.width = 256
        up.height = Math.ceil(pairs.length / 256)
        const uctx = up.getContext('2d')
        pairs.forEach(([R, a], i) => {
            uctx.fillStyle = `rgba(${R}, 0, 0, ${a / 255})`
            uctx.fillRect(i % 256, Math.floor(i / 256), 1, 1)
        })
        result.upload = { width: up.width, height: up.height, count: pairs.length,
            premul: b64(readBack(up, true)), straight: b64(readBack(up, false)) }

        // strokes
        for (const scene of strokeScenes) {
            const canvas = document.createElement('canvas')
            canvas.width = scene.width
            canvas.height = scene.height
            const ctx = canvas.getContext('2d')
            ctx.lineCap = 'round'
            ctx.lineJoin = 'round'
            const f = scene.records
            for (let i = 0; i < f.length; i += 9) {
                ctx.lineWidth = f[i + 4]
                ctx.strokeStyle = `rgba(${f[i + 6]}, ${f[i + 7]}, ${f[i + 8]}, ${f[i + 5]})`
                ctx.beginPath()
                ctx.moveTo(f[i], f[i + 1])
                ctx.lineTo(f[i + 2], f[i + 3])
                ctx.stroke()
            }
            result.strokes.push({ name: scene.name, premul: b64(readBack(canvas, true)) })
        }

        // overlays: the reference asyncInit, awaited to completion.
        for (const c of overlayCases) {
            const mod = await import(`${baseUrl}/shaders/effects/${c.effect}/definition.js`)
            let canvas = null
            let uploads = 0
            await mod.default.asyncInit({
                updateTexture: (name, source) => { canvas = source; uploads++ },
                width: c.width, height: c.height, params: { ...c.params }, isCancelled: () => false,
            })
            result.overlays.push({ name: c.name, uploads, premul: b64(readBack(canvas, true)), straight: b64(readBack(canvas, false)) })
        }
        return result
    }, { baseUrl, strokeScenes, overlayCases })
    oracle.version = browser.version()
} finally {
    await browser.close()
    harness.releaseServer()
}
console.log(`[INFO] oracle: Chromium ${oracle.version} on ${process.platform}-${process.arch}, renderer "${oracle.renderer}"`)
if (!/ANGLE Metal/.test(oracle.renderer)) {
    console.error('The oracle is defined for Chromium with ANGLE Metal (the golden-minting configuration).')
    process.exit(3)
}

// ---------------------------------------------------------------- candidate + compare

const dir = mkdtempSync(join(tmpdir(), 'nmq-async-'))
const dump = (...args) => execFileSync(DUMP, args, { stdio: ['ignore', 'inherit', 'inherit'] })
const fromB64 = (s) => Buffer.from(s, 'base64')
let failures = 0
const report = (ok, label, detail) => {
    console.log(`${ok ? 'PASS' : 'FAIL'} ${label.padEnd(22)} ${detail}`)
    if (!ok) failures++
}

function compareCanvas (ref, got) {
    let differing = 0
    let max = 0
    for (let i = 0; i < ref.length; i++) {
        const d = Math.abs(ref[i] - got[i])
        if (d) { differing++; if (d > max) max = d }
    }
    return { differing, max, ok: ref.length === got.length && max <= MAX_DIFF && differing <= ref.length * MAX_DIFF_FRACTION }
}

// Uploaded bytes may differ only where the premultiplied canvas differs.
function uploadFollowsCanvas (refPremul, gotPremul, refStraight, gotStraight) {
    let extra = 0
    let differing = 0
    let max = 0
    for (let p = 0; p < refPremul.length; p += 4) {
        const canvasSame = refPremul[p] === gotPremul[p] && refPremul[p + 1] === gotPremul[p + 1]
            && refPremul[p + 2] === gotPremul[p + 2] && refPremul[p + 3] === gotPremul[p + 3]
        for (let c = 0; c < 4; c++) {
            const d = Math.abs(refStraight[p + c] - gotStraight[p + c])
            if (!d) continue
            differing++
            if (d > max) max = d
            if (canvasSame) extra++
        }
    }
    return { extra, differing, max }
}

try {
    // math
    const triples = new Float64Array(fromB64(oracle.math).buffer.slice(0))
    const pairs = new Float64Array(triples.length / 3 * 2)
    for (let i = 0, j = 0; i < triples.length; i += 3, j += 2) { pairs[j] = triples[i]; pairs[j + 1] = triples[i + 1] }
    writeFileSync(join(dir, 'math.in'), Buffer.from(pairs.buffer))
    dump('math', join(dir, 'math.in'), join(dir, 'math.out'))
    const results = new Float64Array(readFileSync(join(dir, 'math.out')).buffer.slice(0))
    const kinds = ['sin', 'cos', 'log']
    const mismatch = [0, 0, 0]
    const total = [0, 0, 0]
    for (let i = 0, j = 0; i < triples.length; i += 3, j++) {
        const k = triples[i]
        total[k]++
        if (!Object.is(results[j], triples[i + 2])) mismatch[k]++
    }
    report(mismatch.every(m => m === 0), 'math', kinds.map((k, i) => `${k} ${total[i] - mismatch[i]}/${total[i]} bit-identical`).join(', '))

    // upload conversion
    const up = oracle.upload
    writeFileSync(join(dir, 'upload.premul'), fromB64(up.premul))
    dump('unpremultiply', join(dir, 'upload.premul'), join(dir, 'upload.straight'))
    const refStraight = fromB64(up.straight)
    const gotStraight = readFileSync(join(dir, 'upload.straight'))
    let uploadDiff = 0
    for (let i = 0; i < up.count * 4; i++) if (refStraight[i] !== gotStraight[i]) uploadDiff++
    report(uploadDiff === 0, 'upload', `${up.count} premultiplied (c, a) pairs, ${uploadDiff} channel values differ`)

    // strokes
    for (const scene of strokeScenes) {
        const ref = oracle.strokes.find(s => s.name === scene.name)
        writeFileSync(join(dir, `${scene.name}.f64`), Buffer.from(new Float64Array(scene.records).buffer))
        dump('strokes', join(dir, `${scene.name}.f64`), String(scene.width), String(scene.height), join(dir, `${scene.name}.rgba`))
        const r = compareCanvas(fromB64(ref.premul), readFileSync(join(dir, `${scene.name}.rgba`)))
        report(r.ok, `strokes ${scene.name}`, `${scene.width}x${scene.height} ${scene.records.length / 9} strokes: ` +
            `canvas ${r.differing}/${scene.width * scene.height * 4} channel values differ, max ${r.max}`)
    }

    // overlays
    for (const c of overlayCases) {
        const ref = oracle.overlays.find(o => o.name === c.name)
        const key = c.effect.replace('/', '.')
        const params = JSON.stringify(c.params)
        dump('canvas', key, String(c.width), String(c.height), params, join(dir, `${c.name}.premul`))
        dump('overlay', key, String(c.width), String(c.height), params, join(dir, `${c.name}.straight`))
        const refPremul = fromB64(ref.premul)
        const gotPremul = readFileSync(join(dir, `${c.name}.premul`))
        const canvas = compareCanvas(refPremul, gotPremul)
        const upload = uploadFollowsCanvas(refPremul, gotPremul, fromB64(ref.straight), readFileSync(join(dir, `${c.name}.straight`)))
        let inked = 0
        for (let i = 3; i < refPremul.length; i += 4) if (refPremul[i]) inked++
        report(canvas.ok && upload.extra === 0, `overlay ${c.name}`,
            `${inked} inked px; canvas ${canvas.differing}/${refPremul.length} channel values differ, max ${canvas.max}; ` +
            `upload ${upload.differing} differ, max ${upload.max}, ${upload.extra} outside canvas differences`)
    }
} finally {
    rmSync(dir, { recursive: true, force: true })
}
const total = 2 + strokeScenes.length + overlayCases.length
console.log(`\ncheck_async_overlay: ${total - failures}/${total} PASS`)
process.exit(failures ? 1 : 0)
