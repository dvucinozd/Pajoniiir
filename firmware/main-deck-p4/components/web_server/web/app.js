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

// Rate-limit continuous slider requests so dragging a fader does not flood the
// ESP httpd (5 sockets). Sends the latest value at most every minInterval ms,
// always including a trailing send so the final position is not dropped.
const _throttle = {};
function throttledSend(key, urlFor, value, minInterval = 90) {
    const st = _throttle[key] || (_throttle[key] = { last: 0, timer: null, pending: null });
    const send = (v) => {
        st.last = Date.now();
        fetch(urlFor(v), { method: 'GET' }).catch(err => console.error(err));
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

function init() {
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
                '<tr><td colspan="5" class="loading-cell" style="color: var(--col-red)">Pogreška u komunikaciji.</td></tr>';
        });
}

function renderLibrary(tracks) {
    const tbody = document.getElementById('library-body');
    if (tracks.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" class="loading-cell">Nema pjesama na USB-u.</td></tr>';
        return;
    }

    tbody.innerHTML = tracks.map(track => {
        return `
            <tr>
                <td>${escapeHtml(track.title)}</td>
                <td style="color: var(--text-muted)">${escapeHtml(track.artist)}</td>
                <td>${track.bpm}</td>
                <td>${formatMs(track.duration_ms)}</td>
                <td>
                    <div class="library-actions">
                        <button class="btn btn-load" onclick="loadTrack(${track.index}, 1)">LOAD D1</button>
                        <button class="btn btn-load" onclick="loadTrack(${track.index}, 2)">LOAD D2</button>
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
    const remain = document.getElementById(`deck-${deckNum}-remain`);
    if (dur > 0) {
        const pct = Math.max(0, Math.min(100, (pos / dur) * 100));
        if (fill) fill.style.width = pct + '%';
        if (remain) remain.innerText = '-' + formatMs(dur > pos ? dur - pos : 0);
    } else {
        if (fill) fill.style.width = '0%';
        if (remain) remain.innerText = '';
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
    fetch(`/api/control?deck=${deck}&action=${action}`, { method: 'GET' })
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
    fetch(`/api/control?deck=${deck}&action=seek&value=${ms}`, { method: 'GET' })
        .catch(err => console.error(err));
}

function loadTrack(index, deck) {
    fetch(`/api/load?index=${index}&deck=${deck}`, { method: 'GET' })
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

function toggleBrowserExpand() {
    const body = document.body;
    const btn = document.getElementById('browser-expand-btn');
    if (body.classList.contains('browser-expanded')) {
        body.classList.remove('browser-expanded');
        btn.innerText = 'SHOW';
    } else {
        body.classList.add('browser-expanded');
        btn.innerText = 'HIDE';
    }
}

async function refreshFirmwareStatus() {
    const info = document.getElementById('ota-firmware-info');
    if (!info) return;
    try {
        const response = await fetch('/api/firmware', { cache: 'no-store' });
        if (!response.ok) throw new Error(await response.text());
        const fw = await response.json();
        const s3 = fw.s3 && fw.s3.available
            ? ` | S3 ${fw.s3.version || 'unknown'} from ${fw.s3.slot || 'unknown'} (${fw.s3.state || 'unknown'})`
            : ' | S3 status unavailable';
        info.innerText = `P4 ${fw.running_version || 'unknown'} from ${fw.running_slot || 'unknown'}${s3}`;
    } catch (err) {
        info.innerText = `Firmware status unavailable: ${err.message}`;
    }
}

function uploadP4Firmware() {
    const fileInput = document.getElementById('ota-file');
    const button = document.getElementById('ota-upload-btn');
    const progress = document.getElementById('ota-progress');
    const status = document.getElementById('ota-status');
    const file = fileInput && fileInput.files ? fileInput.files[0] : null;
    if (!file) {
        status.innerText = 'Select a P4 .bin image first.';
        return;
    }
    if (!confirm(`Install ${file.name} (${file.size} bytes) and restart P4?`)) return;

    button.disabled = true;
    progress.value = 0;
    status.innerText = 'Uploading...';
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota/p4');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.setRequestHeader('X-DDJ-OTA', 'p4');
    xhr.upload.onprogress = event => {
        if (event.lengthComputable) progress.value = Math.round(event.loaded * 100 / event.total);
    };
    xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) {
            progress.value = 100;
            status.innerText = 'Image verified. P4 is restarting...';
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

refreshFirmwareStatus();
