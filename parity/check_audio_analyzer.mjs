// check_audio_analyzer.mjs -- AnalyserNode parity gate.
//
// Oracle: Chromium's own AnalyserNode (headless, via the reference
// checkout's Playwright), driven by an OfflineAudioContext that suspends at
// render-quantum boundaries to read all four getters. Candidate:
// nm::AudioAnalyzer (qt/build/tests/audio_analyzer_dump) fed the same
// float32 signal one quantum at a time.
//
// Tolerances (Chromium computes the FFT in float32, the port in double):
//   byte frequency data     |diff| <= 1 (truncation of scaled dB near a step)
//   float frequency data    |diff| <= 0.05 dB, or both -Infinity
//   byte/float time domain  exact
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/check_audio_analyzer.mjs
// Env: NM_AUDIO_ANALYZER_DUMP  candidate binary (default qt/build/tests/audio_analyzer_dump)

import { resolve, dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { existsSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { execFileSync } from 'node:child_process'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..')
const DUMP = process.env.NM_AUDIO_ANALYZER_DUMP || join(REPO, 'qt', 'build', 'tests', 'audio_analyzer_dump')
const BYTE_TOLERANCE = 1
const DB_TOLERANCE = 0.05

if (!process.env.NM_REFERENCE_ROOT) { console.error('NM_REFERENCE_ROOT is not set'); process.exit(3) }
if (!existsSync(DUMP)) { console.error(`audio_analyzer_dump not found at ${DUMP} (build qt/build first)`); process.exit(3) }
const REF = resolve(process.env.NM_REFERENCE_ROOT)
const { chromium } = await import(pathToFileURL(join(REF, 'node_modules', 'playwright', 'index.mjs')).href)

function mulberry32(seed) {
    return () => {
        seed |= 0; seed = seed + 0x6D2B79F5 | 0
        let t = Math.imul(seed ^ seed >>> 15, 1 | seed)
        t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t
        return ((t ^ t >>> 14) >>> 0) / 4294967296
    }
}

// Interleaved float32 test signal: tones with a moving envelope, a sweep,
// noise, silence gaps, per-channel DC offsets, and clipping peaks.
function signal(seed, channels, quanta, sampleRate) {
    const rand = mulberry32(seed)
    const frames = quanta * 128
    const out = new Array(frames * channels)
    const tones = Array.from({ length: channels }, () => [110 + rand() * 900, 1500 + rand() * 6000])
    const dc = Array.from({ length: channels }, () => (rand() - 0.5) * 0.2)
    for (let f = 0; f < frames; f++) {
        const t = f / sampleRate
        const envelope = 0.5 + 0.5 * Math.sin(2 * Math.PI * 0.7 * t)
        const silent = Math.floor(f / 4096) % 5 === 3
        for (let c = 0; c < channels; c++) {
            const [f1, f2] = tones[c]
            const sweep = Math.sin(2 * Math.PI * (200 + 4000 * (f / frames)) * t)
            let v = envelope * (0.6 * Math.sin(2 * Math.PI * f1 * t) + 0.3 * Math.sin(2 * Math.PI * f2 * t))
                + 0.2 * sweep + 0.05 * (rand() * 2 - 1) + dc[c]
            if (f % 9973 === 0) v = c % 2 ? -1.4 : 1.4
            if (silent) v = 0
            out[f * channels + c] = Math.fround(v)
        }
    }
    return out
}

function reads(quanta, step) {
    const out = []
    for (let q = 1; q < quanta; q += step) out.push(q)
    out.push(quanta - 1)
    return [...new Set(out)]
}

const QUANTA = 420
const cases = [
    { name: 'noisedeck-legacy-stereo', options: { fftSize: 256, smoothingTimeConstant: 0.5, minDecibels: -80, maxDecibels: -30 }, channels: 2, seed: 11, sampleRate: 48000 },
    { name: 'noisedeck-channel-mono', options: { fftSize: 256, smoothingTimeConstant: 0.5, minDecibels: -80, maxDecibels: -30 }, channels: 1, seed: 12, sampleRate: 44100 },
    { name: 'reference-demo-mono', options: { fftSize: 256, smoothingTimeConstant: 0.8, minDecibels: -100, maxDecibels: -30 }, channels: 1, seed: 13, sampleRate: 48000 },
    { name: 'analyser-defaults', options: { fftSize: 2048, smoothingTimeConstant: 0.8, minDecibels: -100, maxDecibels: -30 }, channels: 1, seed: 14, sampleRate: 48000 },
    { name: 'quad-no-smoothing', options: { fftSize: 512, smoothingTimeConstant: 0, minDecibels: -90, maxDecibels: -10 }, channels: 4, seed: 15, sampleRate: 48000 },
    { name: 'surround-5.1', options: { fftSize: 1024, smoothingTimeConstant: 0.3, minDecibels: -100, maxDecibels: 0 }, channels: 6, seed: 16, sampleRate: 48000 }
].map(c => ({ ...c, samples: signal(c.seed, c.channels, QUANTA, c.sampleRate), reads: reads(QUANTA, 3 + c.seed % 5) }))

// ---------------------------------------------------------------- oracle

const browser = await chromium.launch({ headless: true })
const oracle = []
try {
    const page = await browser.newPage()
    await page.setContent('<!doctype html><title>analyser oracle</title>')
    for (const c of cases) {
        const result = await page.evaluate(async ({ options, channels, samples, reads, sampleRate }) => {
            const frames = samples.length / channels
            const ctx = new OfflineAudioContext({ numberOfChannels: channels, length: frames, sampleRate })
            const buffer = ctx.createBuffer(channels, frames, sampleRate)
            for (let ch = 0; ch < channels; ch++) {
                const data = buffer.getChannelData(ch)
                for (let f = 0; f < frames; f++) data[f] = samples[f * channels + ch]
            }
            const source = ctx.createBufferSource()
            source.buffer = buffer
            const analyser = ctx.createAnalyser()
            analyser.fftSize = options.fftSize
            analyser.minDecibels = options.minDecibels
            analyser.maxDecibels = options.maxDecibels
            analyser.smoothingTimeConstant = options.smoothingTimeConstant
            source.connect(analyser)
            analyser.connect(ctx.destination)
            const out = []
            for (const q of reads) {
                ctx.suspend(q * 128 / sampleRate).then(() => {
                    const bf = new Uint8Array(analyser.frequencyBinCount)
                    const ff = new Float32Array(analyser.frequencyBinCount)
                    const bt = new Uint8Array(analyser.fftSize)
                    const ft = new Float32Array(analyser.fftSize)
                    analyser.getByteFrequencyData(bf)
                    analyser.getFloatFrequencyData(ff)
                    analyser.getByteTimeDomainData(bt)
                    analyser.getFloatTimeDomainData(ft)
                    out.push({
                        quanta: q,
                        byteFrequency: [...bf],
                        floatFrequency: [...ff].map(v => Number.isFinite(v) ? v : null),
                        byteTimeDomain: [...bt],
                        floatTimeDomain: [...ft]
                    })
                    ctx.resume()
                })
            }
            source.start(0)
            await ctx.startRendering()
            return out
        }, c)
        oracle.push({ name: c.name, reads: result })
    }
    console.log(`[INFO] oracle: ${await browser.version()}`)
} finally {
    await browser.close()
}

// ---------------------------------------------------------------- candidate

const dir = mkdtempSync(join(tmpdir(), 'nmq-analyser-'))
let candidate
try {
    const file = join(dir, 'cases.json')
    writeFileSync(file, JSON.stringify(cases.map(({ name, options, channels, samples, reads }) => ({ name, options, channels, samples, reads }))))
    candidate = JSON.parse(execFileSync(DUMP, [file], { encoding: 'utf8', maxBuffer: 1 << 30 }))
} finally {
    rmSync(dir, { recursive: true, force: true })
}

// ---------------------------------------------------------------- compare

let pass = 0
let total = 0
for (let i = 0; i < cases.length; i++) {
    const expected = oracle[i]
    const actual = candidate[i]
    const stats = { byteMax: 0, byteDiffs: 0, dbMax: 0, timeMismatch: 0, values: 0 }
    let caseOk = expected.reads.length === actual.reads.length
    for (let r = 0; r < expected.reads.length && caseOk; r++) {
        total++
        const e = expected.reads[r]
        const a = actual.reads[r]
        let ok = e.quanta === a.quanta
        for (let k = 0; k < e.byteFrequency.length; k++) {
            const d = Math.abs(e.byteFrequency[k] - a.byteFrequency[k])
            stats.byteMax = Math.max(stats.byteMax, d)
            if (d > 0) stats.byteDiffs++
            if (d > BYTE_TOLERANCE) ok = false
            const ef = e.floatFrequency[k]
            const af = a.floatFrequency[k]
            if ((ef === null) !== (af === null)) ok = false
            else if (ef !== null) {
                stats.dbMax = Math.max(stats.dbMax, Math.abs(ef - af))
                if (Math.abs(ef - af) > DB_TOLERANCE) ok = false
            }
            stats.values++
        }
        for (let k = 0; k < e.floatTimeDomain.length; k++) {
            if (e.floatTimeDomain[k] !== a.floatTimeDomain[k] || e.byteTimeDomain[k] !== a.byteTimeDomain[k]) {
                if (stats.timeMismatch === 0) {
                    console.log(`[DIFF] ${expected.name} quantum ${e.quanta} time index ${k}: float ${e.floatTimeDomain[k]} vs ${a.floatTimeDomain[k]}, byte ${e.byteTimeDomain[k]} vs ${a.byteTimeDomain[k]}`)
                }
                stats.timeMismatch++
                ok = false
            }
        }
        if (ok) pass++
        else if (!stats.reported) {
            stats.reported = true
            console.log(`[FAIL] ${expected.name} read at quantum ${e.quanta}`)
        }
    }
    if (!caseOk) { total++; console.log(`[FAIL] ${expected.name}: read count ${expected.reads.length} vs ${actual.reads.length}`) }
    console.log(`[INFO] ${expected.name}: ${expected.reads.length} reads, byte diffs ${stats.byteDiffs}/${stats.values} (max ${stats.byteMax}), max dB diff ${stats.dbMax.toExponential(2)}, time-domain mismatches ${stats.timeMismatch}`)
}
console.log(`AUDIO_ANALYZER: ${pass}/${total}`)
process.exit(pass === total ? 0 : 1)
