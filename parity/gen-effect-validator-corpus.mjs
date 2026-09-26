// Generator for parity/effect-validator-corpus.json -- the reference-side
// ground truth for the GAP-003 effect-definition validator port.
//
// Run from the repository root with the reference checkout available:
//
//   NM_REFERENCE_ROOT=/path/to/noisemaker node parity/gen-effect-validator-corpus.mjs
//
// It imports the REFERENCE shaders/src/runtime/effect-validator.js
// (validateEffectDefinition at the pinned commit, recorded below), applies the
// same probe set the port's unit test mirrors (qt/tests/
// test_effect_validator.cpp), and records the reference's exact error strings
// per probe. The committed corpus is the differential fixture the port test
// replays; regenerate it after any reference-range update and commit the diff.
//
// Note on ordering: the reference emits errors in Object.entries insertion
// order; the port (QJsonObject) emits sorted key order (documented deviation
// in qt/noisemaker/compiler/effect_validator.h). Consumers therefore compare
// SORTED error lists.

import { readFileSync, writeFileSync } from 'node:fs'
import { execSync } from 'node:child_process'
import { createRequire } from 'node:module'

const refRoot = process.env.NM_REFERENCE_ROOT
if (!refRoot) {
    console.error('NM_REFERENCE_ROOT must point at the reference noisemaker checkout')
    process.exit(1)
}
const require = createRequire(import.meta.url)
const { validateEffectDefinition } = await import(
    `${refRoot}/shaders/src/runtime/effect-validator.js`)

const referenceSha = execSync('git -C "' + refRoot + '" rev-parse HEAD').toString().trim()

// The base definition, identical to the port test's validDefinition().
const base = JSON.parse(readFileSync(new URL('effect-validator-base.json', import.meta.url)))

// Each probe: [id, mutator]. Mutators mirror the port test's case tables.
// JSON cannot carry NaN/Infinity or function-valued hooks (the port test
// exercises those inline); every other probe in the port test is here.
const probes = []
function add(id, mutate) {
    const def = structuredClone(base)
    mutate(def, base)
    probes.push({ id, def })
}
const nestInGlobals = (d, global, spec) => { d.globals[global] = spec }
const firstPass = (d) => d.passes[0]
const setPass = (d, key, value) => { d.passes[0][key] = value }
const setPassNested = (d, container, key, value) => { d.passes[0][container][key] = value }
const setGlobal = (d, global, key, value) => { d.globals[global][key] = value }
const setUi = (d, global, key, value) => { d.globals[global].ui[key] = value }

// --- unknown fields ---
add('unknown top-level field', (d) => { d.globalz = d.globals })
add('unknown global-spec field', (d) => { d.globals.amount.unkown = 1 })
add('unknown ui field', (d) => { d.globals.mode.ui.colour = 'red' })
add('unknown pass field', (d) => { setPass(d, 'progam', 'typo') })
add('unknown texture-spec field', (d) => { d.textures.scratch.widht = 8 })

// --- malformed containers ---
add('globals is an array', (d) => { d.globals = ['nope'] })
add('globals is a string', (d) => { d.globals = 'nope' })
add('globals entry null', (d) => { d.globals = { g: null } })
add('globals entry array', (d) => { d.globals = { g: [] } })
add('ui is a string', (d) => nestInGlobals(d, 'g', { type: 'float', default: 0.5, ui: 'slider' }))
add('passes is an object', (d) => { d.passes = { program: 'x' } })
add('passes contains null', (d) => { d.passes = [null] })
add('inputs is a string', (d) => { d.passes = [{ program: 'p', inputs: 'x' }] })
add('outputs is a number', (d) => { d.passes = [{ program: 'p', outputs: 3 }] })
add('uniforms is a string', (d) => { d.passes = [{ program: 'p', uniforms: 'x' }] })
add('conditions is a string', (d) => { d.passes = [{ program: 'p', conditions: 'always' }] })
add('runIf entry null', (d) => { d.passes = [{ program: 'p', conditions: { runIf: [null] } }] })
add('condition uniform is a number', (d) => {
    d.passes = [{ program: 'p', conditions: { runIf: [{ uniform: 7 }] } }]
})
add('condition missing equals', (d) => {
    d.passes = [{ program: 'p', conditions: { runIf: [{ uniform: 'mode' }] } }]
})
add('textures is an array', (d) => { d.textures = ['x'] })
add('texture spec is a string', (d) => { d.textures = { t: 'big' } })
add('texture width is a word', (d) => { d.textures = { t: { width: 'banana' } } })
add('texture width param is a number', (d) => { d.textures = { t: { width: { param: 42 } } } })
add('texture width has an unknown field', (d) => {
    d.textures = { t: { width: { wrong: 1 } } }
})
add('texture width is negative', (d) => { d.textures = { t: { width: -4 } } })
add('texture format is unknown', (d) => { d.textures = { t: { format: 'rgba999' } } })
add('texture format is a number', (d) => { d.textures = { t: { format: 1 } } })

// --- GAP-004 authorable texture policies (reference 2f47612c) ---
add('filter policy on a 2D texture', (d) => { d.textures = { t: { filter: 'linear' } } })
add('unknown filter policy on a 3D texture', (d) => {
    d.textures3d = { t: { depth: 8, filter: 'cubic' } }
})
add('filter policy is a number on a 3D texture', (d) => {
    d.textures3d = { t: { depth: 8, filter: 2 } }
})
add('valid filter policy on a 3D texture', (d) => {
    d.textures3d = { t: { depth: 8, filter: 'nearest' } }
})
add('mipmaps policy on a 3D texture', (d) => {
    d.textures3d = { t: { depth: 8, mipmaps: true } }
})
add('mipmaps policy is a number on a 2D texture', (d) => {
    d.textures = { t: { mipmaps: 1 } }
})
add('valid mipmaps policy on a 2D texture', (d) => {
    d.textures = { t: { mipmaps: false } }
})
add('persistent policy on a 3D texture', (d) => {
    d.textures3d = { t: { depth: 8, persistent: true } }
})
add('persistent policy is a string on a 2D texture', (d) => {
    d.textures = { t: { persistent: 'yes' } }
})
add('valid persistent policy on a 2D texture', (d) => {
    d.textures = { t: { persistent: true } }
})
add('uniformLayout slot is a string', (d) => {
    d.uniformLayout = { u: { slot: 'x', components: 'x' } }
})
add('uniformLayout components are out of the grammar', (d) => {
    d.uniformLayout = { u: { slot: 0, components: 'xyzwq' } }
})
add('uniformLayout components are out of order', (d) => {
    d.uniformLayout = { u: { slot: 0, components: 'zy' } }
})
add('uniformLayout slot is fractional', (d) => {
    d.uniformLayout = { u: { slot: 0.5, components: 'x' } }
})
add('uniformLayout conflicts at one slot', (d) => {
    d.uniformLayout = {
        u: { slot: 0, components: 'x' },
        v: { slot: 0, components: 'y' },
        w: { slot: 0, components: 'x' },
    }
})
add('uniformLayouts value is a string', (d) => { d.uniformLayouts = { p: 'layout' } })
add('paramAliases is a string', (d) => { d.paramAliases = 'aliases' })
add('paramAliases target is unknown', (d) => { d.paramAliases = { amt: 'nosuchglobal' } })
add('tags is a string', (d) => { d.tags = 'noise' })
add('tags contains an unknown tag', (d) => { d.tags = ['not-a-tag'] })
add('tags contains a number', (d) => { d.tags = [42] })
add('openCategories contains a number', (d) => { d.openCategories = [7] })
add('onInit present (JSON cannot encode functions)', (d) => { d.onInit = 'nope' })
add('onUpdate present (JSON cannot encode functions)', (d) => { d.onUpdate = 42 })
add('onDestroy present (JSON cannot encode functions)', (d) => { d.onDestroy = {} })
add('asyncInit present (JSON cannot encode functions)', (d) => { d.asyncInit = true })
add('shaders is a string', (d) => { d.shaders = 'inline' })
add('shaders value is a string', (d) => { d.shaders = { main: 'source' } })
add('deprecatedBy is a number', (d) => { d.deprecatedBy = 5 })
add('externalTexture is a number', (d) => { d.externalTexture = 7 })

// --- pass field constraints ---
add('pass type is a number', (d) => setPass(d, 'type', 5))
add('pass drawMode is a boolean', (d) => setPass(d, 'drawMode', true))
add('pass input is a number', (d) => setPassNested(d, 'inputs', 'bad', 7))
add('pass input references an undeclared texture', (d) =>
    setPassNested(d, 'inputs', 'bad', 'notDeclaredAnywhere'))
add('pass output references an undeclared texture', (d) =>
    setPassNested(d, 'outputs', 'bad', 'notDeclaredAnywhere'))
add('pass uniform is an array', (d) => setPassNested(d, 'uniforms', 'bad', []))
add('pass uniform is an object', (d) => setPassNested(d, 'uniforms', 'bad', { ref: 1 }))
add('condition uniform is undeclared', (d) =>
    setPassNested(d, 'conditions', 'runIf', [{ uniform: 'nosuch', equals: 1 }]))
add('countUniform is undeclared', (d) => setPass(d, 'countUniform', 'nosuch'))
add('count is a word', (d) => setPass(d, 'count', 'banana'))
add('count is negative', (d) => setPass(d, 'count', -1))
add('repeat is an array', (d) => setPass(d, 'repeat', []))
add('drawMode is unknown', (d) => setPass(d, 'drawMode', 'hexagons'))
add('type is unknown', (d) => setPass(d, 'type', 'vertex'))
add('drawBuffers is zero', (d) => setPass(d, 'drawBuffers', 0))
add('blend is a string', (d) => setPass(d, 'blend', 'on'))
add('blend has one factor', (d) => setPass(d, 'blend', ['ONE']))
add('workgroups is a string', (d) => setPass(d, 'workgroups', '8'))
add('viewport is a number', (d) => setPass(d, 'viewport', 32))
add('entryPoint is a number', (d) => setPass(d, 'entryPoint', 7))

// --- enabledBy / ui ---
add('enabledBy param is undeclared', (d) => setUi(d, 'flag', 'enabledBy', { param: 'nosuch', eq: 1 }))
add('enabledBy is an undeclared name', (d) => setUi(d, 'flag', 'enabledBy', 'nosuch'))
add('enabledBy has no eq', (d) => setUi(d, 'flag', 'enabledBy', { param: 'mode' }))
add('enabledBy has an unknown container', (d) => setUi(d, 'flag', 'enabledBy', { and: 'x' }))
add('ui control is unknown', (d) => setUi(d, 'mode', 'control', 'dropdownx'))
add('ui label is a number', (d) => setUi(d, 'mode', 'label', 7))

// --- global spec type and primitive constraints ---
add('float type is vec9', (d) => setGlobal(d, 'amount', 'type', 'vec9'))
add('float type is a number', (d) => setGlobal(d, 'amount', 'type', 3))
add('float default is a string', (d) => setGlobal(d, 'amount', 'default', 'high'))
add('int default is fractional', (d) => setGlobal(d, 'mode', 'default', 1.5))
add('int default is a string', (d) => setGlobal(d, 'mode', 'default', 'one'))
add('boolean default is a number', (d) => setGlobal(d, 'flag', 'default', 1))
add('color default is short', (d) => setGlobal(d, 'tint', 'default', [1, 0]))
add('color default has a string', (d) => setGlobal(d, 'tint', 'default', [1, 0, 'a']))
add('vec3 default is short', (d) => setGlobal(d, 'point', 'default', [0, 0]))
add('vec3 default is out of min/max', (d) => setGlobal(d, 'point', 'default', [0, 0, 2]))
add('vec3 min is short', (d) => setGlobal(d, 'point', 'min', [-1, -1]))
add('vec3 max is a number', (d) => setGlobal(d, 'point', 'max', 1))
add('float min is a string', (d) => setGlobal(d, 'amount', 'min', 'low'))
add('float min exceeds max', (d) => setGlobal(d, 'amount', 'min', 0.9))
add('float max is below min', (d) => setGlobal(d, 'amount', 'max', 0.4))
add('float step is a string', (d) => setGlobal(d, 'amount', 'step', 'x'))
add('choices is an array', (d) => setGlobal(d, 'mode', 'choices', [0, 1]))
add('choices value is a string', (d) => setGlobal(d, 'mode', 'choices', { bad: 'x' }))
add('int default not among choices', (d) => setGlobal(d, 'mode', 'default', 5))
add('dropdown default not among choices', (d) => setGlobal(d, 'table', 'default', 8))
add('colorModeUniform is a number', (d) => setGlobal(d, 'surfaceIn', 'colorModeUniform', 3))
add('uniform is a number', (d) => setGlobal(d, 'amount', 'uniform', 9))
add('define is a number', (d) => setGlobal(d, 'mode', 'define', 5))

// --- member enum resolution through the std tables ---
add('member enum path unresolved', (d) => nestInGlobals(d, 'memberProbe', {
    type: 'member', default: 'noSuchTable.member', enum: 'noSuchTable',
    ui: { label: 'member', control: 'dropdown' },
}))
add('member enum resolves through std tables', (d) => nestInGlobals(d, 'memberProbe', {
    type: 'member', default: 'oscType.sine', enum: 'oscType',
    ui: { label: 'member', control: 'dropdown' },
}))

// --- duplicate bindings and layout conflicts ---
add('duplicate uniform binding', (d) =>
    nestInGlobals(d, 'other', { type: 'float', default: 0, uniform: 'amount' }))
add('overlapping layout entries', (d) => {
    d.uniformLayout = {
        a: { slot: 1, components: 'x' },
        b: { slot: 1, components: 'xy' },
    }
})
add('duplicate layout entries', (d) => {
    d.uniformLayout = {
        a: { slot: 1, components: 'x' },
        b: { slot: 1, components: 'x' },
    }
})

// --- supported grammar stays clean ---
add('valid plain-object definition', () => {})
add('numeric pass-uniform literals preserved', (d) => {
    d.passes[0].uniforms.literal = 0
    d.passes[0].uniforms.another = 12.5
})
add('supported dimension expressions preserved', (d) => {
    d.textures.expressions = {
        width: { scale: 0.5, clamp: { min: 8, max: 256 } },
        height: { param: 'volumeSize', multiply: 2, paramDefault: 64 },
    }
})

// --- sorted-key-order probe (zzz before aaa in the port, insertion order in
// the reference; consumers compare sorted lists) ---
add('sorted key order probe', (d) => {
    d.globals = {
        zzz: { type: 'float', default: 'bad' },
        aaa: { type: 'float', default: 'bad' },
    }
})

// Run the reference over every probe and record the exact error strings.
const corpus = {
    provenance: {
        reference: 'noisefactorllc/noisemaker',
        referenceCommit: referenceSha,
        generator: 'parity/gen-effect-validator-corpus.mjs',
        note: 'referenceErrors are the reference validateEffectDefinition() output ' +
              'at referenceCommit, verbatim. Consumers compare SORTED error lists ' +
              '(port emits sorted key order; see effect_validator.h deviation note).',
    },
    probes: probes.map((p) => ({ id: p.id, def: p.def, referenceErrors: validateEffectDefinition(p.def) })),
}

// Machine-generated audit of the source-range commits: for each declared and
// observed range, the reference commits and the shaders/ files each touched.
// The job's declared start 4891b99 is a non-contiguous observed range: the
// branch's own prior sync (commit 2c33d74, STATUS.md's 240740dd record)
// already carried 4891b99..240740dd, so the file lists are omitted for that
// pre-synced span (they are graded by the branch's existing byte gates, not
// by this candidate). Regenerated from the reference checkout's own git
// history, so the committed artifact is checkable against upstream without
// this worker.
const git = (args) => execSync(`git -C "${refRoot}" ${args}`).toString()
const rangeAudit = (start, end, { files = true, coveredBy = null } = {}) => {
    const shas = git(`log --reverse --format=%H ${start}..${end}`).trim().split('\n').filter(Boolean)
    const commits = shas.map((sha) => {
        const subject = git(`log -1 --format=%s ${sha}`).trim()
        const shadersFiles = files
            ? git(`show --name-only --format= ${sha} -- shaders/`).split('\n').filter(Boolean)
            : undefined
        return { sha, subject, ...(shadersFiles ? { shadersFiles } : {}) }
    })
    return {
        range: `${start}..${end}`,
        ...(coveredBy ? { coveredBy } : {}),
        commits,
        shadersFilesChanged: files
            ? [...new Set(commits.flatMap((c) => c.shadersFiles))].sort()
            : undefined,
    }
}
const rangeAuditDoc = {
    provenance: {
        reference: 'noisefactorllc/noisemaker',
        referenceCheckout: referenceSha,
        generator: 'parity/gen-effect-validator-corpus.mjs',
        note: 'Commits are listed oldest-first with the shaders/ files each touched. ' +
              'Declared ported range ends at 9d3474df; published main already carries ' +
              'the 2f47612c sync, whose validator delta (GAP-004 texture policies) is ' +
              'also ported and graded by the committed corpus.',
    },
    ranges: [
        rangeAudit('4891b9953f9fd8a61cf9ae0dda2fe747a9be82df', '240740dd2d30cbd0984b179834ab24abe71c8fb2',
            { files: false, coveredBy: 'the branch\'s own prior sync (commit 2c33d74, "vendor: sync upstream noisemaker 4891b995..240740dd"; STATUS.md 240740dd record) — not part of this candidate; graded by the branch\'s byte gates' }),
        rangeAudit('240740dd2d30cbd0984b179834ab24abe71c8fb2', '9d3474dfdc6cb737ebb7b2f3598b16d940af1544',
            { coveredBy: 'this candidate (the GAP-003 validator port)' }),
        rangeAudit('0bd09d00c41c88eb95fa413119ead1344834281c', '9d3474dfdc6cb737ebb7b2f3598b16d940af1544',
            { coveredBy: 'this candidate (observed forced-range start; subset of the 240740dd..9d3474df delta)' }),
        rangeAudit('9d3474dfdc6cb737ebb7b2f3598b16d940af1544', '2f47612c29045c1b91af94887a8ff20106e980ef',
            { coveredBy: 'published main (sync c6ab84a: GAP-004 texture policies in the converter/runtime) plus this candidate\'s validator policy parity' }),
        rangeAudit('2f47612c29045c1b91af94887a8ff20106e980ef', 'fa83eeabf278f1f4999c1d1fff43e2e5338b72ba',
            { coveredBy: 'published main (sync 476261d: GAP-005 pass-field propagation, compiler half; STATUS.md fa83eeabf record)' }),
        rangeAudit('fa83eeabf278f1f4999c1d1fff43e2e5338b72ba', '8eeb7b5ac14eb37a8d16037f607a88ce63924cd3',
            { coveredBy: 'audit only: 8eeb7b5a is docs-only, so the declared range 4891b995..8eeb7b5a adds no shaders/ delta beyond fa83eeabf' }),
        rangeAudit('fa83eeabf278f1f4999c1d1fff43e2e5338b72ba', '6a0af04d3c4f345ffab5e9f8e54e532216b4cdaa',
            { coveredBy: 'this candidate (audit: the GAP-006 texture-pooling and GAP-007 backend-diagnostic commits touch only the web runtime — pipeline.js, backends/webgl2.js, backends/webgpu.js, backends/diagnostics.js — and their tests; the Qt port runs its own native Desktop-GL pipeline in qt/noisemaker/runtime/ and does not consume these web-runtime modules; no shaders/, effect definition, or DSL-compiler file changes)' }),
        rangeAudit('95743621696483b91968992ef6ee0d87b2089fa8', '6a0af04d3c4f345ffab5e9f8e54e532216b4cdaa',
            { coveredBy: 'this candidate (observed forced range; subset of the fa83eeabf..6a0af04d audit above)' }),
    ],
}

const out = new URL('effect-validator-corpus.json', import.meta.url)
writeFileSync(out, JSON.stringify(corpus, null, 1) + '\n')
const auditOut = new URL('effect-validator-range-audit.json', import.meta.url)
writeFileSync(auditOut, JSON.stringify(rangeAuditDoc, null, 1) + '\n')
const failures = corpus.probes.filter((p) => p.referenceErrors.length > 0).length
console.log(`CORPUS: ${corpus.probes.length} probes (${failures} with reference errors) -> ${out.pathname}`)
console.log(`RANGE-AUDIT: ${rangeAuditDoc.ranges.map((r) => r.commits.length).join('/')} commits -> ${auditOut.pathname}`)
