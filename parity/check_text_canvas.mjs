// check_text_canvas.mjs -- filter/text canvas parity gate.
//
// Oracle: a Chromium 2D canvas (Chromium in its new headless mode, via the
// reference checkout's Playwright) drawn with the statements of the
// reference demo host (demo/shaders/lib/demo-ui.js _renderTextToCanvas) and
// the font string Noisedeck builds (style label -> weight and italic), with
// the reference's Nunito (demo/font/Nunito) loaded as a FontFace.
// Candidate: nm::renderTextTexture (qt/build/tests/text_texture_dump) with
// the port's bundled copy of the same font file.
//
// Glyph rasterizers differ, layout must not. Criteria, per case:
//   glyph runs along the text          each run's ink centroid |d| <= 1.0 px
//   ink centroid across the text       |d| <= 1.0 px
//   ink bounding box (alpha > 0)       each edge within 5 px
//   total ink coverage, qt / chrome    0.70 .. 1.05
//   fill colour where both alpha 255   exact
// Runs: each image's alpha is projected onto the rotated text direction
// through the text origin, in 1 px bins; a run is a stretch of bins with ink
// in either image, so both images are cut at the same places. For one line
// of text a run is usually one glyph. A run with ink in only one image fails.
//
// Why per glyph (GAP-027). An ink centroid over a whole line mixes layout
// with rasterization. With w the share of an image's ink in run i and c the
// run's centroid along the text,
//   d = sum_i w_qt,i (c_qt,i - c_chrome,i) + sum_i (w_qt,i - w_chrome,i) c_chrome,i:
// the first term is glyph placement, the second is how the two rasterizers
// share ink between glyphs. For "Heavy" (wght 800, 102 px, rotated -90),
// before the renderer applied the kerning variation deltas, the placement
// term was +1.065 px and the ink term -0.787 px on macOS (CoreText), so the
// line centroid passed at 0.28 px; on Windows (DirectWrite, CI run
// 36038944290) the terms were +1.489 and -0.101 px and it failed. With the
// deltas every glyph is within 0.4 px of Chromium's on macOS, but the terms
// become -0.333 and -0.782 px and the line centroid fails at 1.11 px:
// Chromium's glyph masks are heavier on curves (Qt draws 0.943 of
// Chromium's ink on H, 0.913 .. 0.919 on e, a, v, y), and H sits 112 px from
// the centre. The run centroids measure placement alone.
//
// Why the new headless mode. Playwright's default headless shell places
// glyphs on whole-pixel advances on Linux: measureText gives "H", "He",
// ..., "Heavy" at Nunito 800 102 px as 80, 136, 193, 246 and 304 px, against
// 79.52, 135.47, 192.32, 246.30 and 305.09 on macOS. That moved Linux glyph
// runs by up to 1.19 px against this renderer's layout. Chromium in the new
// headless mode (channel "chromium") positions glyphs at subpixel precision
// on Linux (79.56, 135.56, 192.37, 246.33, 305.10) with no extra switch. The
// headless shell kept whole-pixel advances with --force-device-scale-factor=2
// and with a 2x page scale; fontconfig sets hinting, antialiasing and
// subpixel rendering but not positioning (ui/gfx/font_render_params_linux.cc,
// ui/gfx/linux/fontconfig_util.cc).
//
// Measured 2026-09-24, Chromium 153.0.8010.12, Qt 6.11.1, 10/10 each
// (largest run |d|, largest |d| across the text, coverage):
//   - macOS arm64, CoreText: 0.536, 0.451, 0.757 .. 0.943
//   - macOS arm64, FreeType (QT_QPA_PLATFORM=cocoa:fontengine=freetype):
//     0.727, 0.443, 0.757 .. 0.942
//   - Linux arm64 (ubuntu 24.04), FreeType: 0.346, 0.630, 0.953 .. 0.997
// Chromium's glyph masks are heavier than an exact outline fill, most at
// small sizes: the port draws 0.76 of Chromium's coverage at 26 px on
// macOS. CI runs this gate on Linux (render-smoke) and Windows
// (render-smoke-windows).
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/check_text_canvas.mjs
// Env: NM_TEXT_TEXTURE_DUMP  candidate binary (default qt/build/tests/text_texture_dump)

import { resolve, dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { existsSync, mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const DATA_ROOT = join(REPO, 'qt', 'noisemaker')
const DUMP = process.env.NM_TEXT_TEXTURE_DUMP || join(REPO, 'qt', 'build', 'tests', 'text_texture_dump')
const CENTROID_TOLERANCE = 1.0
const BBOX_TOLERANCE = 5
const COVERAGE_RANGE = [0.70, 1.05]

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }
if (!existsSync(DUMP)) { console.error(`text_texture_dump not found at ${DUMP} (build qt/build first)`); process.exit(3) }
const REF = resolve(process.env.NM_REFERENCE_ROOT)
const { chromium } = await import(pathToFileURL(join(REF, 'node_modules', 'playwright', 'index.mjs')).href)

// The comparison is only meaningful for the same font bytes.
const FONT = 'fonts/Nunito/Nunito-VariableFont_wght.ttf'
const referenceFont = readFileSync(join(REF, 'demo', 'font', 'Nunito', 'Nunito-VariableFont_wght.ttf'))
if (!referenceFont.equals(readFileSync(join(DATA_ROOT, FONT)))) {
    console.error(`FAIL qt/noisemaker/${FONT} differs from the reference demo/font/Nunito copy`)
    process.exit(1)
}

// The oracle below restates these statements of the reference host; stop if
// the reference changed them.
const demo = readFileSync(join(REF, 'demo', 'shaders', 'lib', 'demo-ui.js'), 'utf8')
for (const statement of [
    "ctx.clearRect(0, 0, canvas.width, canvas.height)",
    "const lines = text.split('\\n')",
    "const lineHeight = fontSize * 1.2",
    "const rotation = textState.rotation * Math.PI / 180",
    "ctx.textAlign = justify",
    "ctx.textBaseline = 'middle'",
    "const x = textState.posX * canvas.width",
    "const y = textState.posY * canvas.height",
    "const totalHeight = (lines.length - 1) * lineHeight",
    "const startY = -totalHeight / 2",
    "ctx.fillText(lines[i], 0, lineY)",
    "this._renderer.updateTextureFromSource(texId, textState.canvas, { flipY: true })",
]) {
    if (!demo.includes(statement)) {
        console.error(`FAIL reference demo-ui.js no longer contains: ${statement}`)
        process.exit(1)
    }
}

// css: the font string Noisedeck TextCanvasRenderer builds for the label.
const cases = [
    { name: 'default-1024', width: 1024, height: 1024, css: 'normal 400', u: {} },
    { name: 'default-256', width: 256, height: 256, css: 'normal 400', u: {} },
    { name: 'left-red', width: 1024, height: 1024, css: 'normal 400',
      u: { text: 'Qt Text', size: 0.2, posX: 0.1, posY: 0.3, justify: 'left', color: [1, 0, 0, 1] } },
    { name: 'right-rotated', width: 1024, height: 1024, css: 'normal 400',
      u: { text: 'Rotate', size: 0.15, posX: 0.9, posY: 0.6, rotation: 30, justify: 'right', color: [0, 1, 0.5, 1] } },
    { name: 'multiline', width: 512, height: 512, css: 'normal 400',
      u: { text: 'one\ntwo lines\nthree', size: 0.12 } },
    { name: 'bold', width: 512, height: 512, css: 'normal 700', u: { text: 'Bold', size: 0.2, style: 'Bold' } },
    { name: 'extrabold-rot', width: 512, height: 512, css: 'normal 800',
      u: { text: 'Heavy', size: 0.2, style: 'Condensed ExtraBold', rotation: -90, posX: 0.3 } },
    { name: 'light', width: 512, height: 512, css: 'normal 300', u: { text: 'Light', size: 0.2, style: 'Light' } },
    { name: 'italic', width: 512, height: 512, css: 'italic 400', u: { text: 'Italic', size: 0.2, style: 'Italic' } },
    { name: 'wide-canvas', width: 640, height: 360, css: 'normal 400',
      u: { text: 'min(w, h)', size: 0.25, posX: 0.5, posY: 0.4 } },
]
const DEFAULTS = { text: 'Hello World', font: 'Nunito', size: 0.1, posX: 0.5, posY: 0.5, rotation: 0,
    color: [1, 1, 1, 1], justify: 'center' }
for (const c of cases) c.u = { ...DEFAULTS, ...c.u }

// ---------------------------------------------------------------- oracle

const browser = await chromium.launch({ headless: true, channel: 'chromium' })
const oracle = new Map()
try {
    const page = await browser.newPage()
    await page.setContent('<!doctype html><title>text canvas oracle</title>')
    const results = await page.evaluate(async ({ font, cases }) => {
        const bytes = Uint8Array.from(atob(font), ch => ch.charCodeAt(0))
        const face = new FontFace('Nunito', bytes.buffer, { weight: '200 1000' })
        await face.load()
        document.fonts.add(face)
        const out = []
        for (const c of cases) {
            const u = c.u
            const canvas = document.createElement('canvas')
            canvas.width = c.width
            canvas.height = c.height
            const ctx = canvas.getContext('2d')
            ctx.clearRect(0, 0, canvas.width, canvas.height)
            const lines = String(u.text).split('\n')
            const fontSize = Math.round(u.size * Math.min(canvas.width, canvas.height))
            const lineHeight = fontSize * 1.2
            const rotation = u.rotation * Math.PI / 180
            ctx.font = `${c.css} ${fontSize}px ${u.font}`
            ctx.textAlign = u.justify
            ctx.textBaseline = 'middle'
            ctx.fillStyle = `rgba(${Math.round(u.color[0] * 255)}, ${Math.round(u.color[1] * 255)}, ${Math.round(u.color[2] * 255)}, 1)`
            ctx.save()
            ctx.translate(u.posX * canvas.width, u.posY * canvas.height)
            ctx.rotate(rotation)
            const totalHeight = (lines.length - 1) * lineHeight
            const startY = -totalHeight / 2
            for (let i = 0; i < lines.length; i++) ctx.fillText(lines[i], 0, startY + i * lineHeight)
            ctx.restore()
            const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data
            let binary = ''
            for (let i = 0; i < data.length; i += 0x8000) binary += String.fromCharCode(...data.subarray(i, i + 0x8000))
            out.push({ name: c.name, font: ctx.font, rgba: btoa(binary) })
        }
        return out
    }, { font: referenceFont.toString('base64'), cases })
    for (const r of results) oracle.set(r.name, { font: r.font, rgba: Buffer.from(r.rgba, 'base64') })
    console.log(`[INFO] oracle: Chromium ${await browser.version()} on ${process.platform}-${process.arch}`)
} finally {
    await browser.close()
}

// ---------------------------------------------------------------- candidate

const dir = mkdtempSync(join(tmpdir(), 'nmq-text-'))
const candidate = new Map()
try {
    const file = join(dir, 'cases.json')
    writeFileSync(file, JSON.stringify(cases.map(c => ({ name: c.name, width: c.width, height: c.height, uniforms: c.u }))))
    execFileSync(DUMP, [DATA_ROOT, file, dir], { stdio: ['ignore', 'inherit', 'inherit'] })
    for (const c of cases) candidate.set(c.name, readFileSync(join(dir, `${c.name}.rgba`)))
} finally {
    rmSync(dir, { recursive: true, force: true })
}

// ---------------------------------------------------------------- compare

function ink(rgba, width, height) {
    let coverage = 0
    let left = -1, top = -1, right = -1, bottom = -1
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const a = rgba[(y * width + x) * 4 + 3]
            if (a === 0) continue
            coverage += a / 255
            if (left < 0 || x < left) left = x
            if (x > right) right = x
            if (top < 0) top = y
            bottom = y
        }
    }
    return { coverage, left, top, right, bottom }
}

// Per-glyph layout. Each image's alpha is projected onto the rotated text
// axis through the text origin (pixel centres, 1 px bins) and onto the axis
// across it. The runs are the maximal stretches of bins with ink in either
// image, so both images are cut at the same places; for one line of text a
// run is usually one glyph. Returns each run's ink and alpha-weighted
// centroid along the text for both images, and each image's centroid
// across the text.
function layoutRuns(ref, got, width, height, u) {
    const angle = u.rotation * Math.PI / 180
    const [ux, uy] = [Math.cos(angle), Math.sin(angle)]
    const [ox, oy] = [u.posX * width, u.posY * height]
    const project = (rgba) => {
        const bins = new Map()
        let mass = 0
        let across = 0
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const a = rgba[(y * width + x) * 4 + 3]
                if (a === 0) continue
                const w = a / 255
                const [px, py] = [x + 0.5 - ox, y + 0.5 - oy]
                const t = Math.floor(px * ux + py * uy)
                bins.set(t, (bins.get(t) || 0) + w)
                mass += w
                across += w * (py * ux - px * uy)
            }
        }
        return { bins, across: across / mass }
    }
    const [r, g] = [project(ref), project(got)]
    const keys = [...new Set([...r.bins.keys(), ...g.bins.keys()])].sort((p, q) => p - q)
    const runs = []
    for (const t of keys) {
        let run = runs[runs.length - 1]
        if (!run || t !== run.end + 1) {
            run = { start: t, end: t, chrome: { mass: 0, moment: 0 }, qt: { mass: 0, moment: 0 } }
            runs.push(run)
        }
        run.end = t
        for (const [side, bins] of [['chrome', r.bins], ['qt', g.bins]]) {
            const w = bins.get(t) || 0
            run[side].mass += w
            run[side].moment += w * (t + 0.5)
        }
    }
    for (const run of runs) {
        for (const side of ['chrome', 'qt']) run[side].centroid = run[side].moment / run[side].mass
        run.shift = run.qt.centroid - run.chrome.centroid
    }
    return { runs, across: g.across - r.across }
}

function printLayoutRuns(layout) {
    const show = side => layout.runs.map(r =>
        `[${r.start}..${r.end} ink ${r[side].mass.toFixed(0)} c ${r[side].centroid.toFixed(2)}]`).join(' ')
    console.log(`     ink along the text axis, chrome: ${show('chrome')}`)
    console.log(`     ink along the text axis, qt:     ${show('qt')}`)
    console.log(`     per run, qt - chrome: ${layout.runs.map(r =>
        `c ${r.shift.toFixed(2)} ink x${(r.qt.mass / r.chrome.mass).toFixed(3)}`).join('; ')}`)
}

let failures = 0
for (const c of cases) {
    const ref = oracle.get(c.name)
    const got = candidate.get(c.name)
    const problems = []
    if (!ref || !got || ref.rgba.length !== c.width * c.height * 4 || got.length !== ref.rgba.length) {
        problems.push('missing or mis-sized output')
    } else {
        const a = ink(ref.rgba, c.width, c.height)
        const b = ink(got, c.width, c.height)
        const layout = layoutRuns(ref.rgba, got, c.width, c.height, c.u)
        const worstRun = layout.runs.reduce((m, r) => Math.max(m, Math.abs(r.shift)), 0)
        const edges = [b.left - a.left, b.top - a.top, b.right - a.right, b.bottom - a.bottom]
        const ratio = b.coverage / a.coverage
        let colourMismatch = 0
        for (let i = 0; i < got.length; i += 4) {
            if (ref.rgba[i + 3] !== 255 || got[i + 3] !== 255) continue
            if (ref.rgba[i] !== got[i] || ref.rgba[i + 1] !== got[i + 1] || ref.rgba[i + 2] !== got[i + 2]) colourMismatch++
        }
        if (!(a.coverage > 0)) problems.push('oracle drew nothing')
        if (layout.runs.some(r => !(r.chrome.mass > 0) || !(r.qt.mass > 0))) problems.push('ink run missing in one image')
        else if (!(worstRun <= CENTROID_TOLERANCE)) problems.push('glyph run position')
        if (!(Math.abs(layout.across) <= CENTROID_TOLERANCE)) problems.push('position across the text')
        if (edges.some(e => Math.abs(e) > BBOX_TOLERANCE)) problems.push('bounding box')
        if (!(ratio >= COVERAGE_RANGE[0] && ratio <= COVERAGE_RANGE[1])) problems.push('coverage')
        if (colourMismatch) problems.push(`${colourMismatch} opaque pixels differ in colour`)
        console.log(`${problems.length ? 'FAIL' : 'PASS'} ${c.name.padEnd(14)} ${c.width}x${c.height} ` +
            `font "${ref.font}" runs ${layout.runs.length} max |d| ${worstRun.toFixed(3)} across d=${layout.across.toFixed(3)} ` +
            `bbox d=[${edges.join(', ')}] coverage qt/chrome=${ratio.toFixed(3)}` +
            (problems.length ? `  <- ${problems.join(', ')}` : ''))
        if (problems.length) printLayoutRuns(layout)
    }
    if (problems.length) failures++
}
console.log(`\ncheck_text_canvas: ${cases.length - failures}/${cases.length} PASS`)
process.exit(failures ? 1 : 0)
