let libraryData = [];
let isInteracting = {
    'deck-1-pitch-slider': false,
    'deck-2-pitch-slider': false,
    'deck-1-vol': false,
    'deck-2-vol': false,
    'crossfader': false
};

// Track duration per deck (ms), used to map the progress bar to a seek target.
let deckDuration = { 1: 0, 2: 0 };

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
function throttledSend(key, urlFor, value, minInterval = 90) {
    const st = _throttle[key] || (_throttle[key] = { last: 0, timer: null, pending: null });
    const send = (v) => {
        st.last = Date.now();
        fetch(urlFor(v), mutationOptions).catch(err => console.error(err));
    };
    const elapsed = Date.now() - st.last;
    if (elapsed >= minInterval) {
        send(value);
    } else {
        st.pending = value;
        if (!st.timer) {
            st.timer = setTimeout(() => {
                st.timer = null;
                if (st.pending !== null) { send(st.pending); st.pending = null; }
            }, minInterval - elapsed);
        }
    }
}

function setConnected(ok) {
    const dot = document.getElementById('conn-dot');
    if (!dot) return;
    dot.classList.toggle('online', ok);
    dot.classList.toggle('offline', !ok);
}

// Build the decorative waveform strip once per deck. The bar heights are a fixed
// stylised pattern (the /api/status stream carries no waveform data); the played
// portion is tinted live from the real playback position (see updateWaveform).
function buildWaveform(deckNum) {
    const el = document.getElementById(`deck-${deckNum}-wave`);
    if (!el) return;
    let seed = 91 + deckNum * 7;
    const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
    let html = '';
    for (let i = 0; i < WAVE_BARS; i++) {
        const env = Math.pow(Math.abs(Math.sin(i * 0.09)) * 0.6 + rnd() * 0.55, 1.15);
        const h = Math.max(8, Math.min(100, env * 100));
        const r = h / 100;
        const c = r > 0.8 ? WAVE_PAL.hi : (r > 0.48 ? WAVE_PAL.mid : WAVE_PAL.lo);
        html += `<div class="wave-bar" style="height:${h.toFixed(1)}%;background:${c}"></div>`;
    }
    el.innerHTML = html;
}

function updateWaveform(deckNum, frac) {
    const el = document.getElementById(`deck-${deckNum}-wave`);
    if (!el) return;
    const bars = el.children;
    const lit = Math.round(frac * bars.length);
    for (let i = 0; i < bars.length; i++) {
        bars[i].classList.toggle('played', i < lit);
    }
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

function fetchLibrary() {
    fetch('/api/library')
        .then(res => res.json())
        .then(data => {
            libraryData = data.tracks || [];
            renderLibrary(libraryData);
        })
        .catch(err => {
            console.error('Greška kod dohvaćanja knjižnice:', err);
            document.getElementById('library-body').innerHTML =
                '<tr><td colspan="3" class="loading-cell" style="color: var(--col-red)">Pogreška u komunikaciji.</td></tr>';
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
                        <button class="btn btn-load" onclick="loadTrack(${track.index}, 1)">D1</button>
                        <button class="btn btn-load" onclick="loadTrack(${track.index}, 2)">D2</button>
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
    const peak = (diag && typeof diag.limiter_peak === 'number') ? diag.limiter_peak : 0;
    const level = Math.max(0, Math.min(1, peak / 32767));
    const lit = Math.round(level * 10);
    const segs = container.querySelectorAll('.vu-seg');
    const total = segs.length;
    segs.forEach((seg, i) => {
        // DOM order is top->bottom; light from the bottom up.
        const rankFromBottom = total - 1 - i;
        seg.classList.toggle('vu-active', rankFromBottom < lit);
    });
}

function updateDeckUI(deckNum, data) {
    if (!data) return;

    // Tekstovi i statusi
    document.getElementById(`deck-${deckNum}-title`).innerText = data.title || "No Track";
    document.getElementById(`deck-${deckNum}-artist`).innerText = data.artist || "Unknown Artist";
    // API sends whole BPM (already pitch-adjusted), matching the on-device UI.
    document.getElementById(`deck-${deckNum}-bpm`).innerText = Number(data.bpm).toFixed(2);
    document.getElementById(`deck-${deckNum}-pitch`).innerText = data.pitch_percent >= 0
        ? `+${data.pitch_percent.toFixed(2)}%`
        : `${data.pitch_percent.toFixed(2)}%`;

    // Vrijeme + progress / preostalo
    const pos = data.position_ms || 0;
    const dur = data.duration_ms || 0;
    deckDuration[deckNum] = dur;
    document.getElementById(`deck-${deckNum}-time`).innerText = formatMs(pos);

    const fill = document.getElementById(`deck-${deckNum}-fill`);
    const remainTop = document.getElementById(`deck-${deckNum}-remain-top`);
    if (dur > 0) {
        const frac = Math.max(0, Math.min(1, pos / dur));
        if (fill) fill.style.width = (frac * 100) + '%';
        if (remainTop) remainTop.innerText = '-' + formatMs(dur > pos ? dur - pos : 0);
        updateWaveform(deckNum, frac);
    } else {
        if (fill) fill.style.width = '0%';
        if (remainTop) remainTop.innerText = '';
        updateWaveform(deckNum, 0);
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
    fetch(`/api/control?deck=${deck}&action=${action}`, mutationOptions)
        .then(res => {
            if (!res.ok) console.error(`Control failed: ${action}`);
        })
        .catch(err => console.error(err));
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
    fetch(`/api/control?deck=${deck}&action=seek&value=${ms}`, mutationOptions)
        .catch(err => console.error(err));
}

function loadTrack(index, deck) {
    fetch(`/api/load?index=${index}&deck=${deck}`, mutationOptions)
        .then(res => {
            if (res.ok) {
                console.log(`Učitavanje pjesme ${index} na špil ${deck}`);
            } else {
                alert('Greška prilikom učitavanja pjesme.');
            }
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

// Generic show/hide for the collapsible maintenance cards (USB browser,
// controller profile, P4 firmware update).
function toggleCollapse(cardId, btn) {
    const card = document.getElementById(cardId);
    if (!card) return;
    const collapsed = card.classList.toggle('collapsed');
    if (btn) btn.innerText = collapsed ? 'SHOW' : 'HIDE';
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

function renderProbe(probe) {
    const el = document.getElementById('ota-net-probe');
    if (!el || !probe) return;
    if (probe.state === 'ok') {
        el.textContent = `Connection test passed — address ${probe.address}, and the access point came back.`;
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
// PAJONIIIR before the result can be read back.
async function testOtaNetwork() {
    const status = document.getElementById('ota-net-status');
    if (!confirm('The controller will leave PAJONIIIR for up to 25 seconds to test the update network, then return. This page will be unreachable until it does. Continue?')) return;
    if (status) status.textContent = 'Testing...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify({ probe: true })
        });
        const text = await response.text();
        if (!response.ok) throw new Error(text || response.statusText);
        if (status) status.textContent = 'Test started. Rejoin PAJONIIIR and reload to see the result.';
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
    if (!confirm('The controller will leave PAJONIIIR for up to 30 seconds to check the update server, then return. Nothing is installed. Continue?')) return;
    if (status) status.textContent = 'Checking...';
    try {
        const response = await fetch('/api/ota/config', {
            method: 'POST',
            headers: { 'X-DDJ-Control': '1', 'Content-Type': 'application/json' },
            body: JSON.stringify({ check: true })
        });
        const text = await response.text();
        if (!response.ok) throw new Error(text || response.statusText);
        if (status) status.textContent = 'Check started. Rejoin PAJONIIIR and reload to see the result.';
    } catch (err) {
        if (status) status.textContent = `Check refused: ${err.message}`;
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

// Fills the P4 update card and the read-only S3 card from one /api/firmware
// read. The S3 half is reported separately rather than appended to the P4 line,
// because the two boards drift apart whenever only one of them is updated and a
// single run-on line makes that easy to miss.
async function refreshFirmwareStatus() {
    const info = document.getElementById('ota-firmware-info');
    const s3Info = document.getElementById('s3-firmware-info');
    const s3Badge = document.getElementById('s3-state-badge');
    if (!info && !s3Info) return;
    try {
        const response = await fetch('/api/firmware', { cache: 'no-store' });
        if (!response.ok) throw new Error(await response.text());
        const fw = await response.json();

        if (info) {
            info.innerText = `P4 ${fw.running_version || 'unknown'} from ${fw.running_slot || 'unknown'}`;
        }

        const s3 = fw.s3 || {};
        if (s3Info) {
            s3Info.innerText = s3.available
                ? `${s3.version || 'unknown'} from ${s3.slot || 'unknown'}`
                : 'S3 did not report firmware status. Check the UART control link.';
        }
        if (s3Badge) {
            s3Badge.innerText = s3.available ? (s3.state || 'unknown') : 'offline';
            s3Badge.className = 'badge';
            if (!s3.available) {
                s3Badge.classList.add('error');
            } else if (s3.state === 'valid') {
                s3Badge.classList.add('ready');
            }
            // A matching pair is the normal case; flag a mismatch, since running
            // different builds on the two boards is a real source of confusion.
            if (s3.available && fw.running_version && s3.version &&
                s3.version !== fw.running_version) {
                s3Badge.innerText = 'mismatch';
                s3Badge.className = 'badge error';
            }
        }
    } catch (err) {
        if (info) info.innerText = `Firmware status unavailable: ${err.message}`;
        if (s3Info) s3Info.innerText = `S3 status unavailable: ${err.message}`;
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
            status.innerText = 'Profile validated and stored; S3 activation is queued.';
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

// The S3 card is a live readout, so re-read it periodically — slowly, because
// this shares five httpd sockets with the 250 ms status poll.
setInterval(refreshFirmwareStatus, 15000);
