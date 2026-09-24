// check_text_canvas.mjs -- filter/text canvas parity gate.
//
// Oracle: a Chromium 2D canvas (headless, via the reference checkout's
// Playwright) drawn with the statements of the reference demo host
// (demo/shaders/lib/demo-ui.js _renderTextToCanvas) and the font string
// Noisedeck builds (style label -> weight and italic), with the reference's
// Nunito (demo/font/Nunito) loaded as a FontFace. Candidate:
// nm::renderTextTexture (qt/build/tests/text_texture_dump) with the port's
// bundled copy of the same font file.
//
// Glyph rasterizers differ, layout must not. Tolerances, per case:
//   alpha-weighted ink centroid       |dx|, |dy| <= 1.0 px
//   ink bounding box (alpha > 0)      each edge within 5 px
//   total ink coverage, qt / chrome   0.70 .. 1.05
//   fill colour where both alpha 255  exact
// Measured on macOS arm64, Qt 6.11.1 and Chromium 151, 2026-09-24:
//   - Chromium's glyph masks are heavier than an exact outline fill: the
//     port draws 0.76 .. 0.86 of Chromium's coverage at 26 .. 61 px and
//     0.86 .. 0.95 at 90 .. 205 px.
//   - Qt shapes a variable font with the default instance's GPOS kerning;
//     Chromium applies the instance's kerning deltas. Nunito's default
//     instance is wght 200, so kerned pairs drift with weight: "vy" is
//     kerned +1.92 px by Chromium at wght 400 and +4.40 px at wght 800
//     (102 px), and 0 by Qt. Edges along the text direction can move by
//     that much; the 5 px edge bound covers the cases below and the
//     centroid bound stays at 1 px.
//   - The same 10 cases pass on Qt's FreeType engine (the Linux engine;
//     QT_QPA_PLATFORM=cocoa:fontengine=freetype on macOS). CI runs this
//     gate on Linux in the render-smoke job.
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

const browser = await chromium.launch({ headless: true })
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
    let coverage = 0, sx = 0, sy = 0
    let left = -1, top = -1, right = -1, bottom = -1
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const a = rgba[(y * width + x) * 4 + 3]
            if (a === 0) continue
            const w = a / 255
            coverage += w; sx += w * x; sy += w * y
            if (left < 0 || x < left) left = x
            if (x > right) right = x
            if (top < 0) top = y
            bottom = y
        }
    }
    return { coverage, cx: sx / coverage, cy: sy / coverage, left, top, right, bottom }
}

// Ink runs along the text direction, for diagnosing a failed case: alpha
// mass projected onto the rotated text axis through the text origin, in
// 1 px bins, split where a bin is empty. For one line of text each run is
// usually one glyph, so the runs show whether glyphs moved or differ in ink.
function textAxisRuns(rgba, width, height, u) {
    const angle = u.rotation * Math.PI / 180
    const [ux, uy] = [Math.cos(angle), Math.sin(angle)]
    const [ox, oy] = [u.posX * width, u.posY * height]
    const bins = new Map()
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const a = rgba[(y * width + x) * 4 + 3]
            if (a === 0) continue
            const t = Math.floor((x + 0.5 - ox) * ux + (y + 0.5 - oy) * uy)
            bins.set(t, (bins.get(t) || 0) + a / 255)
        }
    }
    const runs = []
    for (const t of [...bins.keys()].sort((p, q) => p - q)) {
        const last = runs[runs.length - 1]
        if (last && t === last.end + 1) {
            last.end = t
        } else {
            runs.push({ start: t, end: t, mass: 0, moment: 0 })
        }
        const run = runs[runs.length - 1]
        run.mass += bins.get(t)
        run.moment += bins.get(t) * (t + 0.5)
    }
    return runs.map(r => ({ start: r.start, end: r.end, mass: r.mass, centroid: r.moment / r.mass }))
}

function printTextAxisRuns(c, ref, got) {
    const a = textAxisRuns(ref, c.width, c.height, c.u)
    const b = textAxisRuns(got, c.width, c.height, c.u)
    const show = runs => runs.map(r => `[${r.start}..${r.end} ink ${r.mass.toFixed(0)} c ${r.centroid.toFixed(2)}]`).join(' ')
    console.log(`     ink along the text axis, chrome: ${show(a)}`)
    console.log(`     ink along the text axis, qt:     ${show(b)}`)
    if (a.length === b.length) {
        console.log(`     per run, qt - chrome: ${a.map((r, i) =>
            `c ${(b[i].centroid - r.centroid).toFixed(2)} ink x${(b[i].mass / r.mass).toFixed(3)}`).join('; ')}`)
    }
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
        const dx = b.cx - a.cx
        const dy = b.cy - a.cy
        const edges = [b.left - a.left, b.top - a.top, b.right - a.right, b.bottom - a.bottom]
        const ratio = b.coverage / a.coverage
        let colourMismatch = 0
        for (let i = 0; i < got.length; i += 4) {
            if (ref.rgba[i + 3] !== 255 || got[i + 3] !== 255) continue
            if (ref.rgba[i] !== got[i] || ref.rgba[i + 1] !== got[i + 1] || ref.rgba[i + 2] !== got[i + 2]) colourMismatch++
        }
        if (!(a.coverage > 0)) problems.push('oracle drew nothing')
        if (Math.abs(dx) > CENTROID_TOLERANCE || Math.abs(dy) > CENTROID_TOLERANCE) problems.push('centroid')
        if (edges.some(e => Math.abs(e) > BBOX_TOLERANCE)) problems.push('bounding box')
        if (!(ratio >= COVERAGE_RANGE[0] && ratio <= COVERAGE_RANGE[1])) problems.push('coverage')
        if (colourMismatch) problems.push(`${colourMismatch} opaque pixels differ in colour`)
        console.log(`${problems.length ? 'FAIL' : 'PASS'} ${c.name.padEnd(14)} ${c.width}x${c.height} ` +
            `font "${ref.font}" centroid d=(${dx.toFixed(3)}, ${dy.toFixed(3)}) ` +
            `bbox d=[${edges.join(', ')}] coverage qt/chrome=${ratio.toFixed(3)}` +
            (problems.length ? `  <- ${problems.join(', ')}` : ''))
        if (problems.length) printTextAxisRuns(c, ref.rgba, got)
    }
    if (problems.length) failures++
}
console.log(`\ncheck_text_canvas: ${cases.length - failures}/${cases.length} PASS`)
process.exit(failures ? 1 : 0)
