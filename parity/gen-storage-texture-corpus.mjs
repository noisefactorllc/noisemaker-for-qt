import { writeFileSync } from 'node:fs'
import { execFileSync } from 'node:child_process'
import { pathToFileURL } from 'node:url'

const root = process.env.NM_REFERENCE_ROOT
if (!root) throw new Error('NM_REFERENCE_ROOT is required')
const { lex, parse, validate, expand, compileGraph, registerEffect, registerOp, registerStarterOps } =
    await import(pathToFileURL(`${root}/shaders/src/index.js`).href)
const cases = []
const source = 'search synth\nstorageProbe().drawProbe().write(o0)\nrender(o0)\n'
const base = {
    name: 'Storage probe', namespace: 'synth', func: 'storageProbe', starter: true,
    globals: {}, passes: [{ program: 'main', storageTextures: { target: 'outputTex' } }], textures: {}
}
const follower = {
    name: 'Draw probe', namespace: 'synth', func: 'drawProbe', globals: {},
    passes: [{ program: 'main', inputs: { src: 'inputTex' }, outputs: { color: 'outputTex' } }], textures: {}
}
function add(id, mutate = () => {}, dsl = source, finalWrite) {
    const definition = structuredClone(base)
    mutate(definition)
    cases.push({ id, definition, source: dsl, ...(finalWrite === undefined ? {} : { finalWrite }) })
}
add('storage-only output feeds the next node')
for (const [label, target] of [['number', 42], ['null', null], ['object', {}], ['array', []], ['boolean', false]]) {
    add(`invalid ${label} target`, d => { d.passes[0].storageTextures = { target } })
}
add('inline output', () => {}, 'search synth\nstorageProbe().write(o0)\nrender(o0)\n')
add('inline none', () => {}, 'search synth\nstorageProbe().write(none)\nrender(o0)\n')
for (const write of ['o1', { name: 'o1', kind: 'output' }, { name: 'f0', kind: 'feedback' }]) {
    add(`final write ${JSON.stringify(write)}`, () => {}, 'search synth\nstorageProbe().write(o0)\nrender(o0)\n', write)
}
for (const [label, value] of [['absent', undefined], ['null', null], ['false', false], ['zero', 0], ['empty string', ''], ['empty object', {}]]) {
    add(`storage ${label}`, d => {
        if (value === undefined) delete d.passes[0].storageTextures
        else d.passes[0].storageTextures = value
    })
}
for (const [label, value] of [['null', null], ['false', false], ['empty object', {}], ['render output', { color: 'outputTex' }]]) {
    add(`outputs ${label}`, d => { d.passes[0].outputs = value })
}
add('local and global targets', d => {
    d.passes[0].storageTextures = { local: 'scratch', shared: 'global_custom', state: 'global_xyz' }
    d.textures.scratch = { width: 7, height: 9, format: 'rgba32f', usage: ['sample', 'sample', 'storage'] }
})
add('local default usage', d => {
    d.passes[0].storageTextures = { local: 'scratch', undeclared: 'other' }
    d.textures.scratch = { width: 7, height: 9 }
})
add('authored usage without storage', d => {
    d.passes[0].storageTextures = { target: 'scratch' }
    d.textures.scratch = { usage: ['sample', 'copyDst', 'sample'] }
})
add('empty authored usage', d => {
    d.passes[0].storageTextures = { target: 'scratch' }
    d.textures.scratch = { usage: [] }
})
add('declared global and volume policies', d => {
    d.passes[0].storageTextures = { shared: 'global_custom', volume: 'volume', feedback: 'feedback_custom' }
    d.textures.global_custom = { usage: ['sample'] }
    d.textures3d = { volume: { width: 8, height: 8, depth: 8, usage: ['render'] } }
})
add('storage target also rendered', d => {
    d.passes[0].outputs = { color: 'scratch' }
    d.passes[0].storageTextures = { target: 'scratch' }
})
add('multi-pass output writes only at the last pass', d => {
    d.passes.push({ program: 'main', storageTextures: { target: 'outputTex' } })
}, 'search synth\nstorageProbe().write(o0)\nrender(o0)\n')
add('render output overrides the node cursor after inline storage routing', d => {
    d.passes[0].outputs = { color: 'outputTex' }
}, 'search synth\nstorageProbe().write(o0)\nrender(o0)\n')
for (const def of [base, follower]) {
    registerOp(`synth.${def.func}`, { name: def.func, args: [] })
}
registerStarterOps(['synth.storageProbe'])
for (const item of cases) {
    for (const def of [item.definition, follower]) {
        registerEffect(`synth.${def.func}`, { ...def, shaders: { main: {} } })
    }
    const plan = validate(parse(lex(item.source)))
    if (Object.hasOwn(item, 'finalWrite')) {
        plan.plans[0].chain.pop()
        plan.plans[0].write = item.finalWrite
        plan.plans[0].final = plan.plans[0].chain.at(-1).temp
        plan.render = null
    }
    let result
    try {
        result = expand(plan)
    } catch (error) {
        if (!item.id.startsWith('invalid ')) throw error
        item.error = error.message
        continue
    }
    if (item.id.startsWith('invalid ')) throw new Error(`${item.id}: expected expansion to reject the target`)
    if (result.errors.length) throw new Error(`${item.id}: ${result.errors.join('; ')}`)
    item.expected = { passes: result.passes, textureSpecs: result.textureSpecs, renderSurface: result.renderSurface }
    if (!Object.hasOwn(item, 'finalWrite')) item.textures = Object.fromEntries(compileGraph(item.source).textures)
}
const referenceCommit = execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], { encoding: 'utf8' }).trim()
const output = { referenceCommit, follower, cases }
writeFileSync(new URL('storage-texture-corpus.json', import.meta.url), JSON.stringify(output, null, 2) + '\n')
console.log(`Storage texture oracle: ${cases.length} cases at ${referenceCommit}`)
