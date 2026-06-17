let libraryData = [];
let isInteracting = {
    'deck-1-pitch-slider': false,
    'deck-2-pitch-slider': false,
    'deck-1-vol': false,
    'deck-2-vol': false,
    'crossfader': false
};

function init() {
    // Pokreni status loop odmah da sučelje odmah oživi
    pollStatus();
    setInterval(pollStatus, 250);

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
                <td>${track.bpm / 100}</td>
                <td>${formatMs(track.duration_ms)}</td>
                <td>
                    <button class="btn btn-load" onclick="loadTrack(${track.index}, 1)">LOAD D1</button>
                    <button class="btn btn-load" onclick="loadTrack(${track.index}, 2)">LOAD D2</button>
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

function pollStatus() {
    fetch('/api/status')
        .then(res => res.json())
        .then(status => {
            updateDeckUI(1, status.deck1);
            updateDeckUI(2, status.deck2);
            updateMixerUI(status.mixer);
        })
        .catch(err => console.error('Status poll error:', err));
}

function updateDeckUI(deckNum, data) {
    if (!data) return;

    // Tekstovi i statusi
    document.getElementById(`deck-${deckNum}-title`).innerText = data.title || "No Track";
    document.getElementById(`deck-${deckNum}-artist`).innerText = data.artist || "Unknown Artist";
    document.getElementById(`deck-${deckNum}-bpm`).innerText = (data.bpm / 100).toFixed(2);
    document.getElementById(`deck-${deckNum}-pitch`).innerText = data.pitch_percent >= 0 
        ? `+${data.pitch_percent.toFixed(2)}%` 
        : `${data.pitch_percent.toFixed(2)}%`;
    
    // Vrijeme
    document.getElementById(`deck-${deckNum}-time`).innerText = formatMs(data.position_ms);

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
    fetch(`/api/control?deck=${deck}&action=volume&value=${value}`, { method: 'GET' })
        .catch(err => console.error(err));
}

function onCrossfaderChange(value) {
    fetch(`/api/control?action=crossfader&value=${value}`, { method: 'GET' })
        .catch(err => console.error(err));
}

function onPitchChange(deck, value) {
    fetch(`/api/control?deck=${deck}&action=pitch&value=${value}`, { method: 'GET' })
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
