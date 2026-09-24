import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(here, '..', '..');
const webRoot = path.join(
    repoRoot,
    'firmware',
    'main-deck-p4',
    'components',
    'web_server',
    'web'
);

const html = fs.readFileSync(path.join(webRoot, 'index.html'), 'utf8');
const css = fs.readFileSync(path.join(webRoot, 'style.css'), 'utf8');
const app = fs.readFileSync(path.join(webRoot, 'app.js'), 'utf8');

let testsRun = 0;
function test(name, fn) {
    try {
        const result = fn();
        if (result && typeof result.then === 'function') {
            return result.then(() => {
                testsRun++;
                console.log(`PASS ${name}`);
            });
        }
        testsRun++;
        console.log(`PASS ${name}`);
        return Promise.resolve();
    } catch (error) {
        return Promise.reject(new Error(`${name}: ${error.message}`, { cause: error }));
    }
}

function makeClassList() {
    const values = new Set();
    return {
        add: (...names) => names.forEach(name => values.add(name)),
        remove: (...names) => names.forEach(name => values.delete(name)),
        contains: name => values.has(name),
        toggle: (name, force) => {
            const enabled = force === undefined ? !values.has(name) : Boolean(force);
            if (enabled) values.add(name); else values.delete(name);
            return enabled;
        },
    };
}

function makeElement(id) {
    const listeners = new Map();
    const attributes = new Map();
    return {
        id,
        classList: makeClassList(),
        style: {},
        value: 0,
        innerText: '',
        innerHTML: '',
        title: '',
        listeners,
        addEventListener: (name, handler) => listeners.set(name, handler),
        setAttribute: (name, value) => attributes.set(name, String(value)),
        getAttribute: name => attributes.get(name),
        querySelectorAll: () => [],
        querySelector: () => null,
        matches: () => false,
        getBoundingClientRect: () => ({ left: 0, width: 100 }),
        setPointerCapture: () => {},
        scrollIntoView: () => {},
    };
}

function makeRuntime() {
    const elements = new Map();
    const requests = [];
    const errors = [];
    let responseFactory = async () => ({
        ok: true,
        status: 200,
        text: async () => 'OK',
        json: async () => ({}),
    });

    const context = vm.createContext({
        AbortController,
        XMLHttpRequest: class {},
        URL,
        alert: () => {},
        clearTimeout,
        confirm: () => false,
        console: {
            error: message => errors.push(String(message)),
            log: () => {},
        },
        document: {
            activeElement: null,
            documentElement: makeElement('document'),
            fullscreenElement: null,
            webkitFullscreenElement: null,
            readyState: 'loading',
            addEventListener: () => {},
            exitFullscreen: async () => {},
            getElementById: id => elements.get(id) || null,
        },
        fetch: async (url, options) => {
            requests.push({ url: String(url), options });
            return responseFactory(String(url), options);
        },
        setInterval: () => 0,
        setTimeout,
        window: {
            location: { reload: () => {} },
        },
    });
    context.window.document = context.document;
    vm.runInContext(app, context, { filename: 'app.js' });
    return {
        context,
        elements,
        errors,
        requests,
        setResponseFactory: factory => { responseFactory = factory; },
    };
}

await test('HTML uses only embedded stylesheet and script assets', () => {
    const assetRefs = [...html.matchAll(/<(?:script|link)\b[^>]*(?:src|href)="([^"]+)"/gi)]
        .map(match => match[1])
        .filter(ref => !ref.startsWith('https://dvucinozd.github.io/'));
    assert.deepEqual(assetRefs, ['style.css?v=15', 'app.js?v=14']);
    assert.doesNotMatch(css, /@import\s|url\(\s*['"]?https?:/i);
});

await test('viewport keeps browser zoom available', () => {
    assert.doesNotMatch(html, /user-scalable\s*=\s*no/i);
    assert.doesNotMatch(html, /maximum-scale\s*=\s*1/i);
});

await test('every inline UI handler exists in app.js', () => {
    const handlers = new Set(
        [...html.matchAll(/(?:onclick|oninput|onchange)="([A-Za-z_$][\w$]*)\(/g)]
            .map(match => match[1])
    );
    assert.ok(handlers.size > 0);
    for (const handler of handlers) {
        assert.match(app, new RegExp(`function\\s+${handler}\\s*\\(`), `missing ${handler}`);
    }
});

await test('failed SYNC mutation never fabricates local deck state', async () => {
    const runtime = makeRuntime();
    const button = makeElement('deck-1-sync-btn');
    runtime.elements.set(button.id, button);
    runtime.setResponseFactory(async () => ({
        ok: false,
        status: 400,
        text: async () => 'Unknown action',
        json: async () => ({}),
    }));

    vm.runInContext('syncDeck(1)', runtime.context);
    await new Promise(resolve => setTimeout(resolve, 0));
    await new Promise(resolve => setTimeout(resolve, 0));

    assert.equal(runtime.requests.length, 1);
    assert.equal(runtime.requests[0].url, '/api/control?deck=1&action=sync');
    assert.equal(runtime.requests[0].options.method, 'POST');
    assert.equal(runtime.requests[0].options.headers['X-DDJ-Control'], '1');
    assert.equal(button.classList.contains('active'), false);
    assert.equal(button.classList.contains('mutation-error'), true);
    assert.equal(button.classList.contains('syncing'), false);
});

await test('SYNC button renders authoritative deck_core state', () => {
    const runtime = makeRuntime();
    for (const id of [
        'deck-1-title',
        'deck-1-artist',
        'tb-d1-title',
        'deck-1-bpm',
        'deck-1-pitch',
        'deck-1-fill',
        'deck-1-wave',
        'deck-1-needle',
        'deck-1-sync-btn',
        'deck-1-status',
        'deck-1-play-btn',
        'deck-1-pitch-slider',
    ]) {
        runtime.elements.set(id, makeElement(id));
    }

    vm.runInContext(`updateDeckUI(1, {
        title: 'Track', artist: 'Artist', bpm: 128, pitch_percent: 0,
        raw_pitch: 8192, position_ms: 1000, duration_ms: 10000,
        playing: true, state_text: 'PLAYING', sync_enabled: true,
        sync_master: true
    })`, runtime.context);

    const button = runtime.elements.get('deck-1-sync-btn');
    assert.equal(button.classList.contains('active'), true);
    assert.equal(button.classList.contains('sync-master'), true);
    assert.equal(button.getAttribute('aria-pressed'), 'true');
    assert.match(button.title, /SYNC MASTER/);
});

await test('waveform drag previews locally and commits exactly one seek', async () => {
    const runtime = makeRuntime();
    const wave = makeElement('deck-1-wave');
    const needle = makeElement('deck-1-needle');
    const fill = makeElement('deck-1-fill');
    const time = makeElement('deck-1-time');
    runtime.elements.set(wave.id, wave);
    runtime.elements.set(needle.id, needle);
    runtime.elements.set(fill.id, fill);
    runtime.elements.set(time.id, time);

    vm.runInContext(
        'deckDuration[1] = 10000; lastDeckData[1] = { position_ms: 0, duration_ms: 10000, playing: true };',
        runtime.context
    );
    vm.runInContext('attachWaveformScrubbing(1, document.getElementById("deck-1-wave"))', runtime.context);

    wave.listeners.get('pointerdown')({ pointerId: 7, clientX: 20 });
    wave.listeners.get('pointermove')({ pointerId: 7, clientX: 65 });
    assert.equal(runtime.requests.length, 0, 'drag must not seek the decoder');
    wave.listeners.get('pointerup')({ pointerId: 7, clientX: 80 });
    await new Promise(resolve => setTimeout(resolve, 0));

    assert.equal(runtime.requests.length, 1);
    assert.equal(runtime.requests[0].url, '/api/control?deck=1&action=seek&value=8000');
});

await test('waveform cancellation does not seek', () => {
    const runtime = makeRuntime();
    const wave = makeElement('deck-1-wave');
    const needle = makeElement('deck-1-needle');
    const fill = makeElement('deck-1-fill');
    const time = makeElement('deck-1-time');
    runtime.elements.set(wave.id, wave);
    runtime.elements.set(needle.id, needle);
    runtime.elements.set(fill.id, fill);
    runtime.elements.set(time.id, time);

    vm.runInContext(
        'deckDuration[1] = 10000; lastDeckData[1] = { position_ms: 1000, duration_ms: 10000, playing: true, title: "A", artist: "B", bpm: 120, pitch_percent: 0, raw_pitch: 8192, state_text: "PLAYING" };',
        runtime.context
    );
    vm.runInContext('attachWaveformScrubbing(1, document.getElementById("deck-1-wave"))', runtime.context);
    wave.listeners.get('pointerdown')({ pointerId: 9, clientX: 30 });
    wave.listeners.get('pointercancel')({ pointerId: 9, clientX: 0 });
    assert.equal(runtime.requests.length, 0);
});

await test('continuous controls serialize requests and keep the newest value', async () => {
    const runtime = makeRuntime();
    let releaseFirst;
    const firstPending = new Promise(resolve => { releaseFirst = resolve; });
    let calls = 0;
    runtime.setResponseFactory(async () => {
        calls++;
        if (calls === 1) await firstPending;
        return { ok: true, status: 200, text: async () => 'OK', json: async () => ({}) };
    });

    vm.runInContext("throttledSend('fader', v => '/fader=' + v, 100, 0)", runtime.context);
    vm.runInContext("throttledSend('fader', v => '/fader=' + v, 200, 0)", runtime.context);
    vm.runInContext("throttledSend('fader', v => '/fader=' + v, 300, 0)", runtime.context);
    assert.deepEqual(runtime.requests.map(r => r.url), ['/fader=100']);

    releaseFirst();
    await new Promise(resolve => setTimeout(resolve, 0));
    await new Promise(resolve => setTimeout(resolve, 0));
    assert.deepEqual(runtime.requests.map(r => r.url), ['/fader=100', '/fader=300']);
});

await test('late library response cannot overwrite a newer catalog', async () => {
    const runtime = makeRuntime();
    runtime.elements.set('library-body', makeElement('library-body'));
    const resolvers = [];
    runtime.setResponseFactory(() => new Promise(resolve => resolvers.push(resolve)));

    const first = vm.runInContext('fetchLibrary()', runtime.context);
    const second = vm.runInContext('fetchLibrary()', runtime.context);
    resolvers[1]({ ok: true, json: async () => ({ generation: 2, tracks: [] }) });
    await second;
    resolvers[0]({ ok: true, json: async () => ({ generation: 1, tracks: [] }) });
    await first;
    assert.equal(vm.runInContext('libraryGeneration', runtime.context), 2);
});

console.log(`TESTS_RUN=${testsRun}`);
