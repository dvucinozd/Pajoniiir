let libraryData = [];
let libraryGeneration = 0;
let isInteracting = {
    'deck-1-pitch-slider': false,
    'deck-2-pitch-slider': false,
    'deck-1-vol': false,
    'deck-2-vol': false,
    'crossfader': false
};

// Track duration per deck (ms), used to map the progress bar to a seek target.
let deckDuration = { 1: 0, 2: 0 };
let lastDeckData = { 1: null, 2: null };
let timeMode = { 1: 'elapsed', 2: 'elapsed' }; // 'elapsed' | 'remaining'
let isScrubbing = { 1: false, 2: false };

// Waveform palette (matches the on-device "Punchy"-style colour bands).
const WAVE_PAL = { lo: '#1E8F87', mid: '#3FE0D0', hi: '#F5B841' };
const WAVE_BARS = 120;

// Rate-limit continuous slider requests so dragging a fader does not flood the
// ESP httpd (5 sockets). Sends the latest value at most every minInterval ms,
// always including a trailing send so the final position is not dropped.
const _throttle = {};
const mutationOptions = {
    method: 'POST',
    headers: { 'X-DDJ-Control': '1' },
    cache: 'no-store'
};

async function sendMutation(url) {
    const response = await fetch(url, mutationOptions);
    if (!response.ok) {
        const detail = await response.text();
        throw new Error(detail || `HTTP ${response.status}`);
    }
    return response;
}

function throttledSend(key, urlFor, value, minInterval = 90) {
    const st = _throttle[key] || (_throttle[key] = {
        last: -Infinity, timer: null, pending: null, inFlight: false,
        urlFor: null, minInterval: 90
    });
    st.pending = value;
    st.urlFor = urlFor;
    st.minInterval = minInterval;

    const pump = () => {
        if (st.inFlight || st.pending === null) return;
        const remaining = st.minInterval - (Date.now() - st.last);
        if (remaining > 0) {
            if (st.timer === null) {
                st.timer = setTimeout(() => {
                    st.timer = null;
                    pump();
                }, remaining);
            }
            return;
        }

        if (st.timer !== null) {
            clearTimeout(st.timer);
            st.timer = null;
        }
        const next = st.pending;
        st.pending = null;
        st.inFlight = true;
        st.last = Date.now();
        sendMutation(st.urlFor(next))
            .catch(err => console.error(err))
            .finally(() => {
                st.inFlight = false;
                pump();
            });
    };
    pump();
}

function setConnected(ok) {
    const dot = document.getElementById('conn-dot');
    if (!dot) return;
    dot.classList.toggle('online', ok);
    dot.classList.toggle('offline', !ok);
}

// Build the waveform strip and setup interactive needle drop / scrubbing.
function buildWaveform(deckNum) {
    const el = document.getElementById(`deck-${deckNum}-wave`);
    if (!el) return;
    let seed = 91 + deckNum * 7;
    const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
    let html = `<div class="needle-playhead" id="deck-${deckNum}-needle"></div>`;
    for (let i = 0; i < WAVE_BARS; i++) {
        const env = Math.pow(Math.abs(Math.sin(i * 0.09)) * 0.6 + rnd() * 0.55, 1.15);
        const h = Math.max(8, Math.min(100, env * 100));
        const r = h / 100;
        const c = r > 0.8 ? WAVE_PAL.hi : (r > 0.48 ? WAVE_PAL.mid : WAVE_PAL.lo);
        html += `<div class="wave-bar" style="height:${h.toFixed(1)}%;background:${c}"></div>`;
    }
    el.innerHTML = html;
    attachWaveformScrubbing(deckNum, el);
}

function updateWaveform(deckNum, frac) {
    const el = document.getElementById(`deck-${deckNum}-wave`);
    if (!el) return;
    const bars = el.querySelectorAll('.wave-bar');
    const lit = Math.round(frac * bars.length);
    for (let i = 0; i < bars.length; i++) {
        bars[i].classList.toggle('played', i < lit);
    }
    if (!isScrubbing[deckNum]) {
        const needle = document.getElementById(`deck-${deckNum}-needle`);
        if (needle && !el.matches(':hover')) {
            needle.style.left = (frac * 100) + '%';
        }
    }
}

// ── Feature 2: Needle Drop & Interactive Scrubbing ──────────────────────────
function attachWaveformScrubbing(deckNum, waveEl) {
    const needle = document.getElementById(`deck-${deckNum}-needle`);

    const handleScrub = (clientX, commit = false) => {
        const dur = deckDuration[deckNum] || 0;
        if (dur <= 0) return;
        const rect = waveEl.getBoundingClientRect();
        const frac = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
        const ms = Math.floor(frac * dur);

        // Ažuriraj vizualni prikaz vala i igle trenutno
        updateWaveform(deckNum, frac);
        const fill = document.getElementById(`deck-${deckNum}-fill`);
        if (fill) fill.style.width = (frac * 100) + '%';
        if (needle) needle.style.left = (frac * 100) + '%';

        // Ažuriraj digitalni sat odmah u toku povlačenja
        if (lastDeckData[deckNum]) {
            renderDeckTime(deckNum, { ...lastDeckData[deckNum], position_ms: ms });
        }

        // Dragging is a browser-only preview. Decoder seeks are deliberately
        // single-shot so one gesture cannot flood ESP httpd or repeatedly tear
        // down an active decoder. Commit only when the pointer is released.
        if (commit) {
            sendMutation(`/api/control?deck=${deckNum}&action=seek&value=${ms}`)
                .catch(err => console.error(`Seek failed: ${err.message}`));
        }
    };

    waveEl.addEventListener('pointerdown', (e) => {
        try { waveEl.setPointerCapture(e.pointerId); } catch (_) {}
        isScrubbing[deckNum] = true;
        waveEl.classList.add('scrubbing');
        handleScrub(e.clientX, false);
    });

    waveEl.addEventListener('pointermove', (e) => {
        if (isScrubbing[deckNum]) {
            handleScrub(e.clientX, false);
        } else {
            const rect = waveEl.getBoundingClientRect();
            const frac = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
            if (needle) needle.style.left = (frac * 100) + '%';
        }
    });

    const finishScrub = (e) => {
        if (isScrubbing[deckNum]) {
            isScrubbing[deckNum] = false;
            waveEl.classList.remove('scrubbing');
            handleScrub(e.clientX, true);
        }
    };

    const cancelScrub = () => {
        if (!isScrubbing[deckNum]) return;
        isScrubbing[deckNum] = false;
        waveEl.classList.remove('scrubbing');
        const data = lastDeckData[deckNum];
        if (data) {
            const duration = data.duration_ms || 0;
            const fraction = duration > 0
                ? Math.max(0, Math.min(1, (data.position_ms || 0) / duration))
                : 0;
            updateWaveform(deckNum, fraction);
            const fill = document.getElementById(`deck-${deckNum}-fill`);
            if (fill) fill.style.width = (fraction * 100) + '%';
            renderDeckTime(deckNum, data);
        }
    };

    waveEl.addEventListener('pointerup', finishScrub);
    waveEl.addEventListener('pointercancel', cancelScrub);
}

// ── Feature 3: Elapsed vs Remaining Time Toggle ─────────────────────────────
function toggleTimeMode(deckNum) {
    timeMode[deckNum] = (timeMode[deckNum] === 'elapsed') ? 'remaining' : 'elapsed';
    if (lastDeckData[deckNum]) {
        renderDeckTime(deckNum, lastDeckData[deckNum]);
    }
}

function renderDeckTime(deckNum, data) {
    const timeEl = document.getElementById(`deck-${deckNum}-time`);
    const remainTop = document.getElementById(`deck-${deckNum}-remain-top`);
    if (!timeEl) return;

    const pos = data.position_ms || 0;
    const dur = data.duration_ms || 0;
    const rem = Math.max(0, dur - pos);
    const isCritical = (dur > 0 && rem <= 30000 && data.playing);

    if (timeMode[deckNum] === 'remaining') {
        timeEl.innerText = '-' + formatMs(rem);
        timeEl.classList.add('remaining-mode');
        if (remainTop) {
            remainTop.innerText = 'ELAPSED ' + formatMs(pos);
        }
    } else {
        timeEl.innerText = formatMs(pos);
        timeEl.classList.remove('remaining-mode');
        if (remainTop) {
            remainTop.innerText = (dur > 0) ? ('-' + formatMs(rem)) : 'ELAPSED';
        }
    }

    timeEl.classList.toggle('time-critical', isCritical);
}

// ── Feature 4: BPM & Tempo SYNC ─────────────────────────────────────────────
function syncDeck(deckNum) {
    const syncBtn = document.getElementById(`deck-${deckNum}-sync-btn`);
    if (syncBtn) {
        syncBtn.classList.add('syncing');
        syncBtn.classList.remove('mutation-error');
    }

    // The P4 remains authoritative. Do not synthesize BPM/pitch locally: the
    // next /api/status snapshot confirms whether deck_core accepted the event.
    sendMutation(`/api/control?deck=${deckNum}&action=sync`)
        .then(() => scheduleNextPoll())
        .catch(err => {
            console.error(`Sync failed: ${err.message}`);
            if (syncBtn) syncBtn.classList.add('mutation-error');
        })
        .finally(() => {
            if (syncBtn) syncBtn.classList.remove('syncing');
        });
}

function init() {
    buildWaveform(1);
    buildWaveform(2);

    // Pokreni status loop odmah da sučelje odmah oživi. Ulančani setTimeout
    // (umjesto setInterval) šalje sljedeći zahtjev tek nakon što prethodni
    // završi, pa se zahtjevi ne gomilaju na sporom linku (httpd ima 5 socketa).
    scheduleNextPoll();

    // Dohvati podatke o knjižnici u pozadini s kratkom odgodom
    setTimeout(fetchLibrary, 500);

    // Registriraj touch events za blokiranje vanjskih updatea klizača dok korisnik upravlja njima
    const sliders = ['deck-1-pitch-slider', 'deck-2-pitch-slider', 'deck-1-vol', 'deck-2-vol', 'crossfader'];
    sliders.forEach(id => {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('mousedown', () => isInteracting[id] = true);
            el.addEventListener('touchstart', () => isInteracting[id] = true);
            el.addEventListener('mouseup', () => isInteracting[id] = false);
            el.addEventListener('touchend', () => isInteracting[id] = false);
        }
    });
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}

let libraryRequestSequence = 0;
function fetchLibrary() {
    const requestSequence = ++libraryRequestSequence;
    return fetch('/api/library')
        .then(res => {
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            return res.json();
        })
        .then(data => {
            if (requestSequence !== libraryRequestSequence) return;
            libraryGeneration = Number.isInteger(data.generation) ? data.generation : 0;
            libraryData = data.tracks || [];
            renderLibrary(libraryData);
        })
        .catch(err => {
            console.error('Greška kod dohvaćanja knjižnice:', err);
            document.getElementById('library-body').innerHTML =
                '<tr><td colspan="3" class="loading-cell" style="color: var(--col-red)">Communication error.</td></tr>';
        });
}

function renderLibrary(tracks) {
    const tbody = document.getElementById('library-body');
    if (tracks.length === 0) {
        tbody.innerHTML = '<tr><td colspan="3" class="loading-cell">Nema pjesama na USB-u.</td></tr>';
        return;
    }

    tbody.innerHTML = tracks.map(track => {
        return `
            <tr>
                <td>
                    <div class="lib-title">${escapeHtml(track.title)}</div>
                    <div class="lib-artist">${escapeHtml(track.artist)}</div>
                </td>
                <td class="lib-bpm">${track.bpm}</td>
                <td>
                    <div class="library-actions">
                        <button class="btn btn-load btn-load-d1" onclick="loadTrack(${track.track_key}, libraryGeneration, 1)" title="Load Deck 1">D1</button>
                        <button class="btn btn-load btn-load-d2" onclick="loadTrack(${track.track_key}, libraryGeneration, 2)" title="Load Deck 2">D2</button>
                    </div>
                </td>
            </tr>
        `;
    }).join('');
}

function filterLibrary() {
    const query = document.getElementById('search-input').value.toLowerCase().trim();
    if (!query) {
        renderLibrary(libraryData);
        return;
    }

    const filtered = libraryData.filter(track => {
        return (track.title && track.title.toLowerCase().includes(query)) ||
               (track.artist && track.artist.toLowerCase().includes(query));
    });
    renderLibrary(filtered);
}

const POLL_INTERVAL_MS = 250;
let pollTimer = null;

function scheduleNextPoll() {
    if (pollTimer !== null) return;
    pollTimer = setTimeout(() => {
        pollTimer = null;
        pollStatus();
    }, POLL_INTERVAL_MS);
}

function pollStatus() {
    // Abort a stalled request so a slow/dead link can't leave the poll wedged.
    const controller = new AbortController();
    const abortTimer = setTimeout(() => controller.abort(), 2000);
    fetch('/api/status', { signal: controller.signal })
        .then(res => {
            if (!res.ok) throw new Error('HTTP ' + res.status);
            return res.json();
        })
        .then(status => {
            setConnected(true);
            updateDeckUI(1, status.deck1);
            updateDeckUI(2, status.deck2);
            updateMixerUI(status.mixer);
            updateVu(status.diagnostics);
        })
        .catch(err => {
            setConnected(false);
            console.error('Status poll error:', err);
        })
        .finally(() => {
            clearTimeout(abortTimer);
            scheduleNextPoll();   // chain the next poll only after this one settles
        });
}

// Drive the centre meter from the real master limiter peak (0..32767) instead
// of the old hardcoded segments. Honest level indication, not a per-channel VU.
function updateVu(diag) {
    const container = document.getElementById('vu-master');
    if (!container) return;
    const peak = (diag && typeof diag.main_meter_peak === 'number') ? diag.main_meter_peak : 0;
    const level = Math.max(0, Math.min(1, peak / 32767));
    const lit = Math.round(level * 10);
    const segs = container.querySelectorAll('.vu-seg');
    const total = segs.length;
    segs.forEach((seg, i) => {
        // DOM order is top->bottom; light from the bottom up.
        const rankFromBottom = total - 1 - i;
        seg.classList.toggle('vu-active', rankFromBottom < lit);
    });

    const peakText = document.getElementById('mixer-peak-text');
    if (peakText) {
        if (peak <= 0) {
            peakText.innerText = '-inf dB';
        } else {
            const db = Math.round(20 * Math.log10(peak / 32767));
            peakText.innerText = `${db} dB`;
        }
    }
}

function updateDeckUI(deckNum, data) {
    if (!data) return;
    lastDeckData[deckNum] = data;

    // Tekstovi i statusi
    document.getElementById(`deck-${deckNum}-title`).innerText = data.title || "No Track";
    document.getElementById(`deck-${deckNum}-artist`).innerText = data.artist || "Unknown Artist";

    const tbTitle = document.getElementById(`tb-d${deckNum}-title`);
    if (tbTitle) tbTitle.innerText = data.title || "--";
    // API sends whole BPM (already pitch-adjusted), matching the on-device UI.
    document.getElementById(`deck-${deckNum}-bpm`).innerText = Number(data.bpm).toFixed(2);
    document.getElementById(`deck-${deckNum}-pitch`).innerText = data.pitch_percent >= 0
        ? `+${data.pitch_percent.toFixed(2)}%`
        : `${data.pitch_percent.toFixed(2)}%`;

    // Vrijeme + progress / preostalo (podržava Elapsed i Remaining način)
    const pos = data.position_ms || 0;
    const dur = data.duration_ms || 0;
    deckDuration[deckNum] = dur;

    if (!isScrubbing[deckNum]) {
        renderDeckTime(deckNum, data);

        const fill = document.getElementById(`deck-${deckNum}-fill`);
        if (dur > 0) {
            const frac = Math.max(0, Math.min(1, pos / dur));
            if (fill) fill.style.width = (frac * 100) + '%';
            updateWaveform(deckNum, frac);
        } else {
            if (fill) fill.style.width = '0%';
            updateWaveform(deckNum, 0);
        }
    }

    // deck_core state is authoritative; equal BPM alone does not mean SYNC is on.
    const sBtn = document.getElementById(`deck-${deckNum}-sync-btn`);
    if (sBtn && !sBtn.classList.contains('syncing')) {
        sBtn.classList.toggle('active', Boolean(data.sync_enabled));
        sBtn.classList.toggle('sync-master', Boolean(data.sync_master));
        sBtn.setAttribute('aria-pressed', data.sync_enabled ? 'true' : 'false');
        sBtn.title = data.sync_master
            ? 'Ovaj deck je SYNC MASTER'
            : `Sinkroniziraj Deck ${deckNum} s drugim deckom`;
    }

    // Status badge
    const badge = document.getElementById(`deck-${deckNum}-status`);
    badge.innerText = data.state_text || "IDLE";
    badge.className = "badge";
    if (data.playing) {
        badge.classList.add('playing');
    } else if (data.state_text === "READY") {
        badge.classList.add('ready');
    } else if (data.state_text === "ERROR") {
        badge.classList.add('error');
    }

    // Play gumb
    const playBtn = document.getElementById(`deck-${deckNum}-play-btn`);
    if (data.playing) {
        playBtn.innerText = "PAUSE";
        playBtn.classList.add('active');
    } else {
        playBtn.innerText = "PLAY";
        playBtn.classList.remove('active');
    }

    // Pitch slider
    const sliderId = `deck-${deckNum}-pitch-slider`;
    if (!isInteracting[sliderId]) {
        document.getElementById(sliderId).value = data.raw_pitch;
    }
}

function updateMixerUI(data) {
    if (!data) return;

    // Faderi za glasnoću
    if (!isInteracting['deck-1-vol']) {
        document.getElementById('deck-1-vol').value = data.volume1;
    }
    if (!isInteracting['deck-2-vol']) {
        document.getElementById('deck-2-vol').value = data.volume2;
    }

    // Crossfader
    if (!isInteracting['crossfader']) {
        document.getElementById('crossfader').value = data.crossfader;
    }

    // PFL gumbi
    updatePflButton(1, data.pfl1);
    updatePflButton(2, data.pfl2);
}

function updatePflButton(deckNum, active) {
    const btn = document.getElementById(`deck-${deckNum}-pfl-btn`);
    if (active) {
        btn.classList.add('active');
    } else {
        btn.classList.remove('active');
    }
}

// REST Api slanje naredbi
function sendControl(deck, action) {
    sendMutation(`/api/control?deck=${deck}&action=${action}`)
        .catch(err => console.error(`Control failed (${action}): ${err.message}`));
}

function onVolumeChange(deck, value) {
    throttledSend('vol' + deck, v => `/api/control?deck=${deck}&action=volume&value=${v}`, value);
}

function onCrossfaderChange(value) {
    throttledSend('cf', v => `/api/control?action=crossfader&value=${v}`, value);
}

function onPitchChange(deck, value) {
    throttledSend('pitch' + deck, v => `/api/control?deck=${deck}&action=pitch&value=${v}`, value);
}

// Tap/click the progress bar to seek to that position.
function onSeek(deck, event) {
    const dur = deckDuration[deck] || 0;
    if (dur <= 0) return;
    const wrap = document.getElementById(`deck-${deck}-progress`);
    if (!wrap) return;
    const rect = wrap.getBoundingClientRect();
    const frac = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
    const ms = Math.floor(frac * dur);
    sendMutation(`/api/control?deck=${deck}&action=seek&value=${ms}`)
        .catch(err => console.error(`Seek failed: ${err.message}`));
}

function loadTrack(trackKey, generation, deck) {
    const url = `/api/load?track_key=${encodeURIComponent(trackKey)}&generation=${encodeURIComponent(generation)}&deck=${deck}`;
    fetch(url, mutationOptions)
        .then(async res => {
            if (res.ok) {
                console.log(`Učitavanje pjesme ${trackKey} na špil ${deck}`);
                return;
            }
            if (res.status === 409) {
                await fetchLibrary();
                alert('Knjižnica se promijenila. Popis je osvježen; ponovite učitavanje.');
                return;
            }
            alert('Greška prilikom učitavanja pjesme.');
        })
        .catch(err => console.error(err));
}

// Helperi
function formatMs(ms) {
    if (!ms || isNaN(ms)) return "00:00:00";
    let totalSecs = Math.floor(ms / 1000);
    let hrs = Math.floor(totalSecs / 3600);
    let mins = Math.floor((totalSecs % 3600) / 60);
    let secs = totalSecs % 60;

    return `${hrs.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
}

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;')
              .replace(/</g, '&lt;')
              .replace(/>/g, '&gt;')
              .replace(/"/g, '&quot;')
              .replace(/'/g, '&#039;');
}

// Toggle on/off for the maintenance cards (USB browser, controller profile,
// P4 firmware update, update server).
function toggleCard(cardId) {
    const card = document.getElementById(cardId);
    if (!card) return;
    const isCurrentlyCollapsed = card.classList.contains('collapsed');
    const willBeOpen = isCurrentlyCollapsed; // if collapsed, toggle to open (ON)

    card.classList.toggle('collapsed', !willBeOpen);
    updateCardToggleUI(cardId, willBeOpen);

    if (willBeOpen) {
        card.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    }
}

function updateCardToggleUI(cardId, isOpen) {
    // 1. Update the toggle switch in the card header
    const switchBtn = document.getElementById(`toggle-btn-${cardId}`);
    if (switchBtn) {
        switchBtn.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
        switchBtn.classList.toggle('is-on', isOpen);
        const label = switchBtn.querySelector('.toggle-label');
        if (label) label.innerText = isOpen ? 'OPEN' : 'CLOSED';
    }

    // 2. Update the quick tab in the landscape navigation bar
    const navBtn = document.getElementById(`nav-btn-${cardId}`);
    if (navBtn) {
        navBtn.classList.toggle('active-tab', isOpen);
    }
    const badge = document.getElementById(`tab-badge-${cardId}`);
    if (badge) {
        badge.innerText = isOpen ? 'OPEN' : 'CLOSED';
        badge.classList.toggle('badge-on', isOpen);
    }
}

function toggleCollapse(cardId, btn) {
    toggleCard(cardId);
}

// ── Pull-OTA service network ────────────────────────────────────────────────
//
// The passphrase travels in the POST body, never in the URL, and the deck has
// no route that hands it back: the status read reports only whether one is
// stored. That is why the field loads blank, and why an empty field means
// "keep what is saved" rather than "clear it".
async function refreshOtaNetwork() {
    const info = document.getElementById('ota-net-info');
    if (!info) return;
    try {
        const response = await fetch('/api/ota/config', { cache: 'no-store' });
        if (!response.ok) throw new Error(await response.text());
        const cfg = await response.json();
        const ssid = document.getElementById('ota-net-ssid');
        const url = document.getElementById('ota-net-url');
        // Do not overwrite a field the operator is typing into.
        if (ssid && document.activeElement !== ssid) ssid.value = cfg.ssid || '';
        if (url && document.activeElement !== url) url.value = cfg.url || '';
        info.textContent = cfg.ssid
            ? `Network: ${cfg.ssid} — passphrase ${cfg.has_password ? 'stored' : 'not set'}`
            : 'No update server configured.';
        renderProbe(cfg.probe);
    } catch (err) {
        info.textContent = `Update-server settings unavailable: ${err.message}`;
    }
}

// Remembers the release the page was actually shown, so INSTALL can name it
// back. The deck refuses anything else, which stops a stale tab installing
// something the operator never saw.
let otaOfferedRelease = null;

function renderProbe(probe) {
    const el = document.getElementById('ota-net-probe');
    const btn = document.getElementById('ota-net-install');
    if (!el || !probe) return;

    const offer = /^update available: (.+)$/.exec(probe.detail || '');
    otaOfferedRelease = offer ? offer[1] : null;
    if (btn) btn.style.display = otaOfferedRelease ? '' : 'none';
    if (probe.state === 'ok') {
        // Two different operations land here. The link test reports an address;
        // the update check reports what the channel offers and clears it. This
        // renderer was written for the first and silently mislabelled the
        // second as "connection test passed" until the detail was used.
        el.textContent = probe.address
            ? `Connection test passed — address ${probe.address}, and the access point came back.`
            : (probe.detail || 'Done.');
    } else if (probe.state === 'running') {
        el.textContent = `Connection test running: ${probe.detail}`;
    } else if (probe.state === 'failed') {
        el.textContent = `Connection test failed: ${probe.detail}`;
    } else {
        el.textContent = 'Connection test not run.';
    }
}

// Leaves the access point for up to ~25 s. This page therefore goes dead
// mid-test, which is expected rather than a fault: the browser has to rejoin
// Pajoniiir before the result can be read back.
async function testOtaNetwork() {
    const status = document.getElementById('ota-net-status');
    if (!confirm('The controller will leave Pajoniiir for up to 25 seconds to test the update network, then return. This page will be unreachable until it does. Continue?')) return;
    if (status) status.textContent = 'Testing...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify({ probe: true })
        });
        const text = await response.text();
        if (!response.ok) throw new Error(text || response.statusText);
        if (status) status.textContent = 'Test started. Rejoin Pajoniiir and reload to see the result.';
    } catch (err) {
        if (status) status.textContent = `Test refused: ${err.message}`;
    }
}

// Same AP -> STA -> AP round trip as the link test, plus one HTTPS GET of the
// channel document. Reports what is available and installs nothing: an update
// that installs itself the moment it is noticed is the last thing wanted
// mid-set.
async function checkForUpdate() {
    const status = document.getElementById('ota-net-status');
    if (!confirm('The controller will leave Pajoniiir for up to 30 seconds to check the update server, then return. Nothing is installed. Continue?')) return;
    if (status) status.textContent = 'Checking...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify({ check: true })
        });
        const text = await response.text();
        if (!response.ok) throw new Error(text || response.statusText);
        if (status) status.textContent = 'Check started. Rejoin Pajoniiir and reload to see the result.';
    } catch (err) {
        if (status) status.textContent = `Check refused: ${err.message}`;
    }
}

// The irreversible half: writes the inactive slot and reboots into it. Names
// the release back so the deck can refuse anything this page did not display.
async function installUpdate() {
    const status = document.getElementById('ota-net-status');
    if (!otaOfferedRelease) return;
    if (!confirm(`Install ${otaOfferedRelease}? The controller downloads it over your network, verifies the signature, and restarts. Do not cut power until it comes back.`)) return;
    if (status) status.textContent = 'Installing...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify({ install: otaOfferedRelease })
        });
        const text = await response.text();
        if (!response.ok) throw new Error(text || response.statusText);
        if (status) status.textContent = 'Download started. The controller restarts on its own; rejoin Pajoniiir afterwards.';
    } catch (err) {
        if (status) status.textContent = `Install refused: ${err.message}`;
    }
}

async function saveOtaNetwork() {
    const status = document.getElementById('ota-net-status');
    const ssid = document.getElementById('ota-net-ssid');
    const pass = document.getElementById('ota-net-pass');
    const url = document.getElementById('ota-net-url');
    if (!ssid || !pass || !url) return;
    const payload = { ssid: ssid.value.trim(), url: url.value.trim() };
    // Only send a passphrase when one was typed, so correcting an SSID does
    // not require retyping it.
    if (pass.value.length > 0) payload.password = pass.value;
    if (status) status.textContent = 'Saving...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        const text = await response.text();
        if (!response.ok) throw new Error(text || response.statusText);
        // Do not leave the secret in the DOM once the deck has it.
        pass.value = '';
        if (status) status.textContent = 'Saved.';
        refreshOtaNetwork();
    } catch (err) {
        if (status) status.textContent = `Rejected: ${err.message}`;
    }
}

async function clearOtaNetwork() {
    const status = document.getElementById('ota-net-status');
    if (!confirm('Forget the update network, passphrase and URL?')) return;
    if (status) status.textContent = 'Clearing...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify({ clear: true })
        });
        if (!response.ok) throw new Error(await response.text());
        const pass = document.getElementById('ota-net-pass');
        if (pass) pass.value = '';
        if (status) status.textContent = 'Cleared.';
        refreshOtaNetwork();
    } catch (err) {
        if (status) status.textContent = `Failed: ${err.message}`;
    }
}

// Fills the P4 update card from /api/firmware.
async function refreshFirmwareStatus() {
    const info = document.getElementById('ota-firmware-info');
    if (!info) return;
    try {
        const response = await fetch('/api/firmware', { cache: 'no-store' });
        if (!response.ok) throw new Error(await response.text());
        const fw = await response.json();

        if (info) {
            info.innerText = `P4 ${fw.running_version || 'unknown'} from ${fw.running_slot || 'unknown'}`;
        }

    } catch (err) {
        if (info) info.innerText = `Firmware status unavailable: ${err.message}`;
    }
}

function uploadP4Firmware() {
    const fileInput = document.getElementById('ota-file');
    const button = document.getElementById('ota-upload-btn');
    const progress = document.getElementById('ota-progress');
    const status = document.getElementById('ota-status');
    const file = fileInput && fileInput.files ? fileInput.files[0] : null;
    if (!file) {
        status.innerText = 'Select a signed P4 .ddjota bundle first.';
        return;
    }
    if (!file.name.toLowerCase().endsWith('.ddjota')) {
        status.innerText = 'Unsigned .bin images are rejected. Select the P4 .ddjota bundle.';
        return;
    }
    if (!confirm(`Install ${file.name} (${file.size} bytes) and restart P4?`)) return;

    button.disabled = true;
    progress.value = 0;
    status.innerText = 'Uploading...';
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/p4');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-DDJ-Control', '1');
    xhr.setRequestHeader('X-DDJ-OTA', 'p4');
    xhr.upload.onprogress = event => {
        if (event.lengthComputable) progress.value = Math.round(event.loaded * 100 / event.total);
    };
    xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) {
            progress.value = 100;
            status.innerText = 'Signature and image verified. P4 is restarting...';
            setTimeout(() => window.location.reload(), 8000);
        } else {
            button.disabled = false;
            status.innerText = `Update rejected: ${xhr.responseText || xhr.status}`;
        }
    };
    xhr.onerror = () => {
        button.disabled = false;
        status.innerText = 'Upload connection failed. The current firmware remains bootable.';
    };
    xhr.send(file);
}

async function refreshControllerProfiles() {
    const info = document.getElementById('profile-list-info');
    if (!info) return;
    try {
        const response = await fetch('/api/controller-profiles', { cache: 'no-store' });
        if (!response.ok) throw new Error(await response.text());
        const data = await response.json();
        const profiles = Array.isArray(data.profiles) ? data.profiles : [];
        info.innerText = profiles.length
            ? `SD profiles: ${profiles.map(p => `${p.id}${p.valid ? '' : ' (invalid)'}`).join(', ')}`
            : 'No controller profiles found on the SD card.';
    } catch (err) {
        info.innerText = `Profile list unavailable: ${err.message}`;
    }
}

function uploadControllerProfile() {
    const idInput = document.getElementById('profile-id');
    const fileInput = document.getElementById('profile-file');
    const overwriteInput = document.getElementById('profile-overwrite');
    const button = document.getElementById('profile-upload-btn');
    const progress = document.getElementById('profile-progress');
    const status = document.getElementById('profile-status');
    const id = idInput ? idInput.value.trim() : '';
    const file = fileInput && fileInput.files ? fileInput.files[0] : null;
    const overwrite = Boolean(overwriteInput && overwriteInput.checked);

    if (!/^[A-Za-z0-9_-]{1,39}$/.test(id)) {
        status.innerText = 'Profile ID may contain only letters, digits, _ and - (max 39).';
        return;
    }
    if (!file) {
        status.innerText = 'Select a compiled profile.s3bin first.';
        return;
    }
    if (!file.name.toLowerCase().endsWith('.s3bin')) {
        status.innerText = 'Select a compiled .s3bin profile, not profile.json.';
        return;
    }
    if (file.size < 32 || file.size > 16384) {
        status.innerText = 'Profile must be between 32 and 16384 bytes.';
        return;
    }

    const action = overwrite ? 'OVERWRITE' : 'install';
    if (!confirm(`${action} SD profile "${id}" with ${file.name} (${file.size} bytes)?`)) return;

    button.disabled = true;
    progress.value = 0;
    status.innerText = 'Uploading and validating...';
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/controller-profile');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-DDJ-Control', '1');
    xhr.setRequestHeader('X-DDJ-Profile-ID', id);
    xhr.setRequestHeader('X-DDJ-Profile-Overwrite', overwrite ? '1' : '0');
    xhr.upload.onprogress = event => {
        if (event.lengthComputable) progress.value = Math.round(event.loaded * 100 / event.total);
    };
    xhr.onload = () => {
        button.disabled = false;
        if (xhr.status >= 200 && xhr.status < 300) {
            progress.value = 100;
            status.innerText = 'Profile validated, stored, and activated on P4.';
            refreshControllerProfiles();
            refreshFirmwareStatus();
        } else if (xhr.status === 409 && !overwrite) {
            status.innerText = 'Profile already exists. Enable overwrite and confirm again.';
        } else {
            status.innerText = `Profile rejected: ${xhr.responseText || xhr.status}`;
        }
    };
    xhr.onerror = () => {
        button.disabled = false;
        status.innerText = 'Upload connection failed. The previous SD profile remains intact.';
    };
    xhr.send(file);
}

refreshFirmwareStatus();
refreshControllerProfiles();
refreshOtaNetwork();

// Refresh firmware state periodically without competing with the fast status poll.
setInterval(refreshFirmwareStatus, 15000);

// ── Mobile Landscape UI Helpers ──────────────────────────────────────────────
function toggleFullscreen() {
    if (!document.fullscreenElement && !document.webkitFullscreenElement) {
        const elem = document.documentElement;
        if (elem.requestFullscreen) {
            elem.requestFullscreen().catch(() => {});
        } else if (elem.webkitRequestFullscreen) {
            elem.webkitRequestFullscreen();
        }
    } else {
        if (document.exitFullscreen) {
            document.exitFullscreen().catch(() => {});
        } else if (document.webkitExitFullscreen) {
            document.webkitExitFullscreen();
        }
    }
}

function dismissOrientBanner() {
    const banner = document.getElementById('orient-banner');
    if (banner) banner.style.display = 'none';
}

function openDrawer(cardId) {
    toggleCard(cardId);
}

// ── Services Toggle ──────────────────────────────────────────────────────────
let servicesVisible = false;

function toggleServices() {
    servicesVisible = !servicesVisible;
    const navBar = document.getElementById('landscape-nav-bar');
    const btn = document.getElementById('btn-services');

    if (navBar) {
        navBar.classList.toggle('services-active', servicesVisible);
    }
    if (btn) {
        btn.classList.toggle('active-services', servicesVisible);
    }

    // When closing services, collapse and hide any active service cards
    if (!servicesVisible) {
        const serviceCardIds = ['profile-card', 'ota-card', 'ota-net-card'];
        serviceCardIds.forEach(id => {
            const card = document.getElementById(id);
            if (card && !card.classList.contains('collapsed')) {
                card.classList.add('collapsed');
                updateCardToggleUI(id, false);
            }
        });
    }
}
