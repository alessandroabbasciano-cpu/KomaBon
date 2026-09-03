// Mobile nav: the sidebar becomes an off-canvas panel below 768px, driven by
// the .nav-open class on <body>. On desktop the class is inert.
function toggleNav() {
    setNav(!document.body.classList.contains('nav-open'));
}

function closeNav() {
    setNav(false);
}

function setNav(open) {
    document.body.classList.toggle('nav-open', open);
    const btn = document.querySelector('.nav-toggle');
    if (btn) btn.setAttribute('aria-expanded', open ? 'true' : 'false');
}

document.addEventListener('keydown', e => {
    if (e.key === 'Escape') closeNav();
});

function showTab(tabId) {
    closeNav();
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-links li').forEach(el => el.classList.remove('active'));

    document.getElementById(tabId).classList.add('active');
    const navItems = ['dashboard', 'ereader', 'settings'];
    document.querySelectorAll('.nav-links li')[navItems.indexOf(tabId)].classList.add('active');

    // Load data when switching tabs
    if (tabId === 'ereader') {
        fetchBooks();
        getReaderProgress();
    } else if (tabId === 'settings') {
        getWifiStatus();
        getDisplaySettings();
    }
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// escapeHtml() is for text: escapes & < > but lets quotes and single quotes pass through,
// which is precisely what breaks a value inside an attribute. File names
// and EPUB titles frequently contain single quotes ("O'Brien"), so anything
// going into an attribute passes through here.
function escapeAttr(text) {
    const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
    return String(text).replace(/[&<>"']/g, c => map[c]);
}

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        document.getElementById('battery-val').innerText = data.battery + '%' + (data.charging ? ' (Charging)' : '');
        document.getElementById('uptime-val').innerText = data.uptime;

        // Update version display
        if (data.version) {
            document.getElementById('current-ver').innerText = data.version;
            document.getElementById('version-display').innerText = data.version;
        }

        // Format free space in KB or MB
        const freeKB = Math.round(data.freeSpace / 1024);
        const totalKB = Math.round(data.totalSpace / 1024);
        document.getElementById('freespace-val').innerText = freeKB + ' / ' + totalKB + ' KB';

        // Update Header
        let voltageText = data.voltage.toFixed(2) + 'V';
        if (data.charging) {
            voltageText += ' ⚡';
            document.getElementById('header-voltage').style.color = '#00ff00'; // Bright Green for charging
        } else {
            document.getElementById('header-voltage').style.color = ''; // Default
        }
        document.getElementById('header-voltage').innerText = voltageText;

        const batIcon = document.getElementById('battery-icon');
        const level = parseInt(data.battery);

        // Snap to grid for CSS classes
        let visualLevel = 0;
        if (level > 90) visualLevel = 100;
        else if (level > 70) visualLevel = 80;
        else if (level > 50) visualLevel = 60;
        else if (level > 30) visualLevel = 40;
        else if (level > 10) visualLevel = 20;
        else visualLevel = 0;

        batIcon.setAttribute('data-level', visualLevel);

        // Update battery icon charging state
        if (data.charging) {
            batIcon.classList.add('charging');
        } else {
            batIcon.classList.remove('charging');
        }

    } catch (e) {
        console.error("Failed to fetch status", e);
    }
}

async function checkUpdate() {
    const btn = document.getElementById('check-update-btn');
    const msg = document.getElementById('update-status');
    const updateBtn = document.getElementById('update-btn');

    btn.innerText = "Checking...";
    msg.innerText = "";
    updateBtn.classList.add('hidden');

    try {
        const res = await fetch('/api/check_update');
        const data = await res.json();

        if (data.hasUpdate) {
            let updateParts = [];
            if (data.hasFirmware) updateParts.push("firmware");
            if (data.hasFilesystem) updateParts.push("web interface");

            // Release tags and body text come from outside the device:
            // treated as content, not markup.
            msg.innerHTML = `<strong>New version available: ${escapeHtml(data.latest)}</strong>`;
            if (updateParts.length > 0) {
                msg.innerHTML += `<br><small>Includes: ${updateParts.join(" and ")}</small>`;
            }
            if (data.release_notes) {
                msg.innerHTML += `<br><small>${escapeHtml(data.release_notes)}</small>`;
            }
            msg.style.color = "var(--success)";
            updateBtn.classList.remove('hidden');
            btn.innerText = "Check Again";
        } else {
            msg.innerText = "You are up to date.";
            msg.style.color = "var(--text-secondary)";
            btn.innerText = "Check Again";
        }
    } catch (e) {
        msg.innerText = "Error checking update.";
        msg.style.color = "var(--danger)";
        btn.innerText = "Retry";
    }
}

async function performUpdate() {
    if (!confirm("Install update? Device will restart when complete.")) return;

    const msg = document.getElementById('update-status');
    const updateBtn = document.getElementById('update-btn');

    msg.innerText = "Downloading and installing update...";
    msg.style.color = "var(--accent)";
    updateBtn.classList.add('hidden');

    fetch('/api/update/all', { method: 'POST' });
    alert("Update started. The device will reboot when complete. This page will stop responding during the update.");
}

// === Ereader Book Management ===
// v1.2.2: case-insensitive extension checks (match firmware behavior)
const isEpub = f => f.toLowerCase().endsWith('.epub');
const isFont = f => f.toLowerCase().endsWith('.ttf');
const isKmb = f => f.toLowerCase().endsWith('.kmb');

let currentBooks = [];          // Server-provided order (books + fonts)
let saveOrderTimer = null;      // Debounce: avoid hammering flash on rapid clicks

async function fetchBooks() {
    const bookList = document.getElementById('book-list');
    bookList.innerHTML = '<p>Loading...</p>';

    try {
        const res = await fetch('/api/books');
        const data = await res.json();
        currentBooks = (data.books || []);
        renderBooks();
    } catch (e) {
        bookList.innerHTML = '<p class="error">Error loading books.</p>';
        console.error("Failed to fetch books", e);
    }
}

function renderBooks() {
    const bookList = document.getElementById('book-list');
    if (!currentBooks.length) {
        bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        return;
    }

    // We keep this filter to ensure reordering only applies to EPUBs
    const epubs = currentBooks.filter(b => isEpub(b.filename));

    bookList.innerHTML = currentBooks.map(book => {
        const bookIsFont = isFont(book.filename);
        const bookIsKmb = isKmb(book.filename);
        const nameAttr = escapeAttr(book.filename);

        let orderBtns = '';
        // Reordering is only enabled for EPUBs to prevent backend mismatch
        if (isEpub(book.filename) && epubs.length > 1) {
            const idx = epubs.indexOf(book);
            orderBtns = `
                <span class="order-btns">
                    <button class="btn-order" ${idx === 0 ? 'disabled' : ''} data-action="move" data-dir="-1" data-filename="${nameAttr}" title="Move up">▲</button>
                    <button class="btn-order" ${idx === epubs.length - 1 ? 'disabled' : ''} data-action="move" data-dir="1" data-filename="${nameAttr}" title="Move down">▼</button>
                </span>`;
        }

        // Assign visual icons based on file type
        let displayIcon = '📖 ';
        if (bookIsFont) displayIcon = '📂 [Font] ';
        if (bookIsKmb) displayIcon = '🖼️ [Comic] ';

        return `
        <div class="book-item">
            ${orderBtns}
            <span class="book-title">${displayIcon}${escapeHtml(book.name)}</span>
            <span class="book-size">${Math.round(book.size / 1024)} KB</span>
            <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(book.name)}">Delete</button>
        </div>
    `}).join('');

    bindBookListActions();
}

// The listener stays on the container, which survives innerHTML replacement, 
// so binding it once is sufficient.
let bookListBound = false;
function bindBookListActions() {
    if (bookListBound) return;
    const bookList = document.getElementById('book-list');
    if (!bookList) return;
    bookList.addEventListener('click', e => {
        const btn = e.target.closest('button[data-action]');
        if (!btn) return;
        if (btn.dataset.action === 'delete') {
            deleteBook(btn.dataset.filename, btn.dataset.name);
        } else if (btn.dataset.action === 'move') {
            moveBook(btn.dataset.filename, Number(btn.dataset.dir));
        }
    });
    bookListBound = true;
}

function moveBook(filename, dir) {
    // Swap within the .epub subsequence only; fonts keep their positions.
    const epubIdxs = currentBooks
        .map((b, i) => isEpub(b.filename) ? i : -1)
        .filter(i => i >= 0);
    const pos = epubIdxs.findIndex(i => currentBooks[i].filename === filename);
    const target = pos + dir;
    if (pos < 0 || target < 0 || target >= epubIdxs.length) return;

    const a = epubIdxs[pos], b = epubIdxs[target];
    [currentBooks[a], currentBooks[b]] = [currentBooks[b], currentBooks[a]];
    renderBooks();
    scheduleSaveOrder();
}

function scheduleSaveOrder() {
    clearTimeout(saveOrderTimer);
    saveOrderTimer = setTimeout(saveBookOrder, 500);
}

async function saveBookOrder() {
    const order = currentBooks
        .filter(b => isEpub(b.filename))
        .map(b => b.filename);
    try {
        await fetch('/api/books/order', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ order })
        });
    } catch (e) {
        console.error("Failed to save book order", e);
    }
}

function uploadBook() {
    const fileInput = document.getElementById('book-file');
    const status = document.getElementById('upload-status');
    const progressContainer = document.getElementById('upload-progress');
    const progressBar = document.getElementById('upload-progress-bar');

    if (!fileInput.files.length) {
        status.innerText = "Please select a file.";
        status.style.color = "var(--danger)";
        return;
    }

    const file = fileInput.files[0];
    if (!isEpub(file.name) && !isFont(file.name)) {
        status.innerText = "Only .epub and .ttf files are supported.";
        status.style.color = "var(--danger)";
        return;
    }

    // Show progress bar and reset
    progressContainer.classList.remove('hidden');
    progressBar.style.width = '0%';
    status.innerText = "Uploading...";
    status.style.color = "var(--accent)";

    const formData = new FormData();
    formData.append('file', file);

    // Use XMLHttpRequest for progress tracking
    const xhr = new XMLHttpRequest();

    // Track upload progress
    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percentComplete = (e.loaded / e.total) * 100;
            progressBar.style.width = percentComplete + '%';
            status.innerText = `Uploading... ${Math.round(percentComplete)}%`;
        }
    });

    // Handle completion
    xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
            progressBar.style.width = '100%';
            status.innerText = "Upload complete!";
            status.style.color = "var(--success)";
            fileInput.value = '';

            // Hide progress bar after a delay
            setTimeout(() => {
                progressContainer.classList.add('hidden');
            }, 2000);

            fetchBooks();
        } else {
            progressContainer.classList.add('hidden');
            status.innerText = "Upload failed: " + xhr.responseText;
            status.style.color = "var(--danger)";
        }
    });

    // Handle errors
    xhr.addEventListener('error', () => {
        progressContainer.classList.add('hidden');
        status.innerText = "Upload error.";
        status.style.color = "var(--danger)";
        console.error("Upload failed");
    });

    // Send the request
    xhr.open('POST', '/api/books/upload');
    xhr.send(formData);
}

async function deleteBook(filename, displayName) {
    // Use display name for confirmation, filename for API call
    const nameToShow = displayName || filename;
    if (!confirm(`Delete "${nameToShow}"?`)) return;

    try {
        const res = await fetch('/api/books/delete?name=' + encodeURIComponent(filename), {
            method: 'DELETE'
        });

        if (res.ok) {
            fetchBooks();
        } else {
            alert("Failed to delete book.");
        }
    } catch (e) {
        alert("Error deleting book.");
        console.error("Delete failed", e);
    }
}

// Initial Load
setInterval(fetchStatus, 5000);
fetchStatus();
getReaderSettings();
getReaderProgress();
getSleepSettings();
getWifiStatus();
getDisplaySettings();

function getReaderSettings() {
    fetch('/api/settings/reader')
        .then(response => response.json())
        .then(data => {
            if (data.refreshFrequency) {
                document.getElementById('refresh-rate').value = data.refreshFrequency;
            }
            if (data.fontSize) {
                document.getElementById('font-size').value = data.fontSize;
            }
            if (data.fontFamily !== undefined) {
                document.getElementById('font-family').value = data.fontFamily;
            }
        })
        .catch(error => console.error('Error loading reader settings:', error));
}

function saveReaderSettings() {
    const refreshRate = parseInt(document.getElementById('refresh-rate').value);
    const fontSize = parseInt(document.getElementById('font-size').value);
    const fontFamily = parseInt(document.getElementById('font-family').value);
    const statusDiv = document.getElementById('reader-settings-status');

    fetch('/api/settings/reader', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ refreshFrequency: refreshRate, fontSize: fontSize, fontFamily: fontFamily }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "green";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "red";
            }
        })
        .catch(error => {
            console.error('Error saving settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "red";
        });
}

function getReaderProgress() {
    fetch('/api/reader/progress')
        .then(response => response.json())
        .then(data => {
            const status = document.getElementById('reader-progress-status');
            if (!status) return;

            if (data.exists) {
                const name = data.displayName || data.lastBook || 'Saved book';
                const page = data.page || 1;
                status.textContent = `${name} - page ${page}${data.resumeOnBoot ? ' (will resume on boot)' : ''}`;
            } else {
                status.textContent = 'No saved reading position.';
            }
        })
        .catch(error => console.error('Error loading reader progress:', error));
}

function resetReaderProgress() {
    if (!confirm('Reset saved reading progress? This will not delete any books.')) return;

    const statusDiv = document.getElementById('reader-progress-reset-status');
    fetch('/api/reader/progress', { method: 'DELETE' })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = 'Reading progress reset.';
                statusDiv.style.color = 'green';
                getReaderProgress();
                setTimeout(() => statusDiv.textContent = '', 3000);
            } else {
                statusDiv.textContent = 'Error resetting progress.';
                statusDiv.style.color = 'red';
            }
        })
        .catch(error => {
            console.error('Error resetting reader progress:', error);
            statusDiv.textContent = 'Connection error.';
            statusDiv.style.color = 'red';
        });
}

// === Library State (v1.8.0) ===
// Export/import of reading progress + book metadata + manual order.
function exportLibraryState() {
    const status = document.getElementById('library-state-status');
    status.style.color = '';
    status.textContent = 'Preparing export...';

    fetch('/api/library/export')
        .then(response => {
            if (!response.ok) throw new Error('HTTP ' + response.status);
            return response.blob();
        })
        .then(blob => {
            // The device has no RTC, so the date in the filename comes from the
            // browser.
            const stamp = new Date().toISOString().slice(0, 10);
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `komabon-state-${stamp}.json`;
            document.body.appendChild(a);
            a.click();
            a.remove();
            URL.revokeObjectURL(url);
            status.textContent = 'State exported.';
            status.style.color = 'green';
            setTimeout(() => status.textContent = '', 4000);
        })
        .catch(error => {
            console.error('Export failed', error);
            status.textContent = 'Export failed.';
            status.style.color = 'red';
        });
}

function importLibraryState(input) {
    const file = input.files && input.files[0];
    if (!file) return;
    input.value = '';  // allow re-picking the same file after a failure

    const status = document.getElementById('library-state-status');
    status.style.color = '';

    // The device caps the bundle at 64 KB; fail here rather than after the
    // upload. The server remains the authority.
    if (file.size > 64 * 1024) {
        status.textContent = 'File too large (limit 64 KB).';
        status.style.color = 'red';
        return;
    }
    if (!confirm('Import reading state? For each book the furthest-ahead page wins.')) return;

    status.textContent = 'Importing...';

    const form = new FormData();
    form.append('state', file, file.name);

    fetch('/api/library/import', { method: 'POST', body: form, credentials: 'include' })
        .then(response => response.json().then(body => ({ ok: response.ok, body })))
        .then(({ ok, body }) => {
            if (!ok || body.status !== 'ok') {
                status.textContent = 'Import failed: ' + (body.message || 'unknown error');
                status.style.color = 'red';
                return;
            }
            let msg = `${body.merged} updated, ${body.added} added, ${body.skipped} already ahead`;
            if (body.pending > 0) {
                msg += `. ${body.pending} waiting for the .epub to be uploaded`;
            }
            status.textContent = msg + '.';
            status.style.color = 'green';
            getReaderProgress();
            fetchBooks();
        })
        .catch(error => {
            console.error('Import failed', error);
            status.textContent = 'Connection error.';
            status.style.color = 'red';
        });
}

// === Sleep Settings ===
function getSleepSettings() {
    fetch('/api/settings/sleep')
        .then(response => response.json())
        .then(data => {
            if (data.sleepTimeout !== undefined) {
                document.getElementById('sleep-timeout').value = data.sleepTimeout;
            }
            if (data.sleepMessage !== undefined) {
                document.getElementById('sleep-message').value = data.sleepMessage;
            }
        })
        .catch(error => console.error('Error loading sleep settings:', error));
}

function saveSleepSettings() {
    const sleepTimeout = parseInt(document.getElementById('sleep-timeout').value);
    const sleepMessage = document.getElementById('sleep-message').value;
    const statusDiv = document.getElementById('sleep-settings-status');

    fetch('/api/settings/sleep', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ sleepTimeout: sleepTimeout, sleepMessage: sleepMessage }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "green";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "red";
            }
        })
        .catch(error => {
            console.error('Error saving sleep settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "red";
        });
}

// === Display Orientation ===
function getDisplaySettings() {
    fetch('/api/settings/display')
        .then(response => response.json())
        .then(data => {
            if (data.rotation !== undefined) {
                document.getElementById('display-rotation').value = data.rotation;
            }
        })
        .catch(error => console.error('Error loading display settings:', error));
}

function saveDisplaySettings() {
    const rotation = parseInt(document.getElementById('display-rotation').value);
    const statusDiv = document.getElementById('display-settings-status');

    fetch('/api/settings/display', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ rotation: rotation }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Orientation applied.";
                statusDiv.style.color = "green";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error applying orientation.";
                statusDiv.style.color = "red";
            }
        })
        .catch(error => {
            console.error('Error saving display settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "red";
        });
}

// === Wi-Fi / Hotspot ===
function getWifiStatus() {
    fetch('/api/wifi/status')
        .then(response => response.json())
        .then(data => {
            const el = document.getElementById('wifi-status');
            if (!el) return;
            if (data.sta_connected) {
                el.textContent = `Connected to "${data.sta_ssid}" (${data.sta_ip}), signal ${data.rssi} dBm.`;
            } else if (data.ap_active) {
                el.textContent = `Hotspot mode — network "${data.ap_ssid}" at ${data.ap_ip}. Join a Wi-Fi network below to get online.`;
            } else {
                el.textContent = 'Not connected.';
            }
        })
        .catch(error => console.error('Error loading Wi-Fi status:', error));
}

function scanWifi() {
    const sel = document.getElementById('wifi-ssid');
    const status = document.getElementById('wifi-connect-status');
    status.style.color = 'var(--accent)';
    status.textContent = 'Scanning…';

    let tries = 0;
    const poll = () => {
        fetch('/api/wifi/scan')
            .then(response => response.status === 202 ? null : response.json())
            .then(data => {
                if (!data) {
                    if (tries++ < 10) { setTimeout(poll, 1000); return; }
                    status.textContent = 'Scan timed out. Try again.';
                    status.style.color = 'var(--danger)';
                    return;
                }
                const nets = (data.networks || []).filter(n => n.ssid);
                if (nets.length === 0) {
                    status.textContent = 'No networks found.';
                    status.style.color = 'var(--text-secondary)';
                    return;
                }
                // The SSID goes inside an attribute, so it passes through
                // escapeAttr: escapeHtml lets quotes through, and an SSID is
                // text that any device within range can choose —
                // someone could just name it `"><img onerror=...>` to inject
                // markup into this page.
                sel.innerHTML = nets.map(n =>
                    `<option value="${escapeAttr(n.ssid)}">${escapeHtml(n.ssid)} (${n.rssi} dBm)${n.secure ? ' 🔒' : ''}</option>`
                ).join('');
                status.textContent = `Found ${nets.length} network(s).`;
                status.style.color = 'var(--success)';
            })
            .catch(error => {
                console.error('Wi-Fi scan failed:', error);
                status.textContent = 'Scan error.';
                status.style.color = 'var(--danger)';
            });
    };
    poll();
}

function connectWifi() {
    const ssid = document.getElementById('wifi-ssid').value;
    const password = document.getElementById('wifi-pass').value;
    const status = document.getElementById('wifi-connect-status');

    if (!ssid) {
        status.textContent = 'Select a network first (tap Scan).';
        status.style.color = 'var(--danger)';
        return;
    }

    status.textContent = `Connecting to "${ssid}"…`;
    status.style.color = 'var(--accent)';

    fetch('/api/wifi/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, password: password })
    })
        .then(response => response.json())
        .then(() => {
            let tries = 0;
            const poll = () => fetch('/api/wifi/status')
                .then(response => response.json())
                .then(data => {
                    if (data.sta_connected) {
                        status.textContent = `Connected! KomaBon is online at ${data.sta_ip}. You can rejoin your home Wi-Fi on your phone.`;
                        status.style.color = 'var(--success)';
                        getWifiStatus();
                    } else if (tries++ < 15) {
                        setTimeout(poll, 1000);
                    } else {
                        status.textContent = 'Could not connect — check the password and try again.';
                        status.style.color = 'var(--danger)';
                    }
                })
                .catch(() => { if (tries++ < 15) setTimeout(poll, 1000); });
            poll();
        })
        .catch(error => {
            console.error('Wi-Fi connect failed:', error);
            status.textContent = 'Connection request failed.';
            status.style.color = 'var(--danger)';
        });
}

// === KomaBon Universal Converter Engine ===

// Configure PDF.js worker
pdfjsLib.GlobalWorkerOptions.workerSrc = 'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

// Write logs to the virtual terminal
function logMessage(msg, isError = false) {
    const terminal = document.getElementById('terminal-log');
    const status = document.getElementById('comic-status');

    terminal.classList.remove('hidden');

    status.innerText = msg;
    status.style.color = isError ? "var(--danger)" : "var(--accent)";

    const line = document.createElement('div');
    const time = new Date().toLocaleTimeString();
    line.innerText = `[${time}] ${msg}`;

    if (isError) {
        line.style.color = "var(--danger)";
    }

    terminal.appendChild(line);
    terminal.scrollTop = terminal.scrollHeight;
}

// Router function triggered by the UI button
async function processInputFile() {
    const fileInput = document.getElementById('universal-file');
    const terminal = document.getElementById('terminal-log');

    terminal.innerHTML = '';

    if (!fileInput.files.length) {
        logMessage("Error: Select a file to process.", true);
        return;
    }

    const file = fileInput.files[0];
    const ext = file.name.split('.').pop().toLowerCase();

    logMessage(`--- Starting processing for: ${file.name} ---`);

    try {
        if (ext === 'cbz' || ext === 'zip') {
            await processArchive(file);
        } else if (ext === 'pdf') {
            await processPDF(file);
        } else if (ext === 'epub' || ext === 'odt' || ext === 'rtf') {
            await processTextDocument(file, ext);
        } else {
            logMessage(`Unsupported extension: ${ext}`, true);
        }
    } catch (err) {
        logMessage("Converter Error: " + err.message, true);
    }
}

// Text document processor routing
async function processTextDocument(file, ext) {
    if (ext === 'epub') {
        await optimizeEPUB(file);
    } else {
        logMessage(`Conversion from ${ext.toUpperCase()} to EPUB pending implementation.`, true);
    }
}

// EPUB Optimizer: preserves XML/HTML for text reflow, resizes and dithers images
async function optimizeEPUB(file) {
    const targetWidth = parseInt(document.getElementById('eink-width').value);
    const targetHeight = parseInt(document.getElementById('eink-height').value);
    const progressBar = document.getElementById('comic-progress-bar');
    const progressContainer = document.getElementById('comic-progress');

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '5%';

    logMessage(`Optimizing EPUB images: ${file.name}...`);

    try {
        const zip = await JSZip.loadAsync(file);
        
        // Find all image files within the EPUB container
        const imageFiles = Object.keys(zip.files).filter(name => 
            name.match(/\.(jpg|jpeg|png|gif|webp)$/i)
        );

        logMessage(`Found ${imageFiles.length} images. Processing...`);

        let processed = 0;
        for (let imgPath of imageFiles) {
            const imgData = await zip.file(imgPath).async("blob");
            const bitmap = await createImageBitmap(imgData);

            // Scale to fit within display limits without upscaling
            let scale = Math.min(targetWidth / bitmap.width, targetHeight / bitmap.height);
            if (scale > 1.0) scale = 1.0; 

            const finalWidth = Math.round(bitmap.width * scale);
            const finalHeight = Math.round(bitmap.height * scale);

            const canvas = document.createElement('canvas');
            canvas.width = finalWidth;
            canvas.height = finalHeight;
            const ctx = canvas.getContext('2d', { willReadFrequently: true });
            
            // White background to clear transparent PNG artifacts
            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(0, 0, finalWidth, finalHeight);
            ctx.drawImage(bitmap, 0, 0, finalWidth, finalHeight);

            // Extract pixel data for Dithering
            const imageData = ctx.getImageData(0, 0, finalWidth, finalHeight);
            const data = imageData.data;

            // Step 1: Grayscale conversion
            for (let i = 0; i < data.length; i += 4) {
                const luma = (data[i] * 0.299) + (data[i + 1] * 0.587) + (data[i + 2] * 0.114);
                data[i] = data[i+1] = data[i+2] = luma;
            }

            // Step 2: Floyd-Steinberg Dithering for 1-bit E-ink
            for (let py = 0; py < finalHeight; py++) {
                for (let px = 0; px < finalWidth; px++) {
                    const pIdx = (py * finalWidth + px) * 4;
                    const oldPixel = data[pIdx];
                    
                    const newPixel = oldPixel < 128 ? 0 : 255;
                    data[pIdx] = data[pIdx + 1] = data[pIdx + 2] = newPixel;
                    
                    const quantError = oldPixel - newPixel;

                    if (px + 1 < finalWidth) {
                        data[pIdx + 4] += quantError * (7 / 16);
                        data[pIdx + 5] += quantError * (7 / 16);
                        data[pIdx + 6] += quantError * (7 / 16);
                    }
                    if (py + 1 < finalHeight) {
                        if (px - 1 >= 0) {
                            data[pIdx + (finalWidth * 4) - 4] += quantError * (3 / 16);
                            data[pIdx + (finalWidth * 4) - 3] += quantError * (3 / 16);
                            data[pIdx + (finalWidth * 4) - 2] += quantError * (3 / 16);
                        }
                        data[pIdx + (finalWidth * 4)] += quantError * (5 / 16);
                        data[pIdx + (finalWidth * 4) + 1] += quantError * (5 / 16);
                        data[pIdx + (finalWidth * 4) + 2] += quantError * (5 / 16);
                        if (px + 1 < finalWidth) {
                            data[pIdx + (finalWidth * 4) + 4] += quantError * (1 / 16);
                            data[pIdx + (finalWidth * 4) + 5] += quantError * (1 / 16);
                            data[pIdx + (finalWidth * 4) + 6] += quantError * (1 / 16);
                        }
                    }
                }
            }
            ctx.putImageData(imageData, 0, 0);

            // Compress back to high-quality JPEG to preserve dithering artifacts
            const newImgBlob = await new Promise(resolve => canvas.toBlob(resolve, 'image/jpeg', 1.0));
            zip.file(imgPath, newImgBlob);
            
            bitmap.close();
            processed++;
            progressBar.style.width = `${5 + (processed / imageFiles.length * 80)}%`;
        }

        logMessage(`Repackaging optimized EPUB...`);
        const newEpubBlob = await zip.generateAsync({ 
            type: "blob", 
            compression: "DEFLATE", 
            compressionOptions: { level: 6 } 
        });

        progressBar.style.width = '95%';
        logMessage(`Uploading to KomaBon...`);

        // Force strictly .epub extension
        let rawName = file.name.replace(/\.epub$/i, '');
        let safeName = rawName
            .normalize("NFD").replace(/[\u0300-\u036f]/g, "")
            .replace(/[^a-zA-Z0-9_\-]/g, "_")
            + '.epub';

        await uploadKMB(newEpubBlob, safeName, progressBar);

    } catch (err) {
        logMessage("EPUB Processing Error: " + err.message, true);
    }
}

// PDF Parser
async function processPDF(file) {
    const targetWidth = parseInt(document.getElementById('eink-width').value);
    const targetHeight = parseInt(document.getElementById('eink-height').value);
    const progressBar = document.getElementById('comic-progress-bar');
    const progressContainer = document.getElementById('comic-progress');

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '5%';

    try {
        logMessage("Loading PDF document into memory...");
        const arrayBuffer = await file.arrayBuffer();
        const pdf = await pdfjsLib.getDocument({ data: arrayBuffer }).promise;

        const pageCount = pdf.numPages;
        logMessage(`PDF loaded. Found ${pageCount} pages.`);

        const bytesPerRow = Math.ceil(targetWidth / 8);
        const bytesPerPage = bytesPerRow * targetHeight;
        const totalSize = 16 + (bytesPerPage * pageCount);

        logMessage(`Allocating KMB binary buffer: ${Math.round(totalSize / 1024)} KB.`);
        const kmbBuffer = new ArrayBuffer(totalSize);
        const kmbView = new DataView(kmbBuffer);
        const kmbBytes = new Uint8Array(kmbBuffer);

        // Write KMB header
        kmbView.setUint8(0, 'K'.charCodeAt(0));
        kmbView.setUint8(1, 'M'.charCodeAt(0));
        kmbView.setUint8(2, 'B'.charCodeAt(0));
        kmbView.setUint8(3, '1'.charCodeAt(0));
        kmbView.setUint16(4, 3, true);
        kmbView.setUint16(6, targetWidth, true);
        kmbView.setUint16(8, targetHeight, true);
        kmbView.setUint16(10, pageCount, true);
        kmbView.setUint32(12, 0, true);

        let offset = 16;
        const canvas = document.createElement('canvas');
        canvas.width = targetWidth;
        canvas.height = targetHeight;
        const ctx = canvas.getContext('2d', { willReadFrequently: true });

        for (let i = 1; i <= pageCount; i++) {
            progressBar.style.width = `${10 + (i / pageCount * 80)}%`;
            logMessage(`Rendering and dithering PDF page ${i}/${pageCount}...`);

            const page = await pdf.getPage(i);

            let baseViewport = page.getViewport({ scale: 1.0 });
            let pageRotation = baseViewport.rotation;

            if (baseViewport.width > baseViewport.height) {
                pageRotation = (pageRotation + 270) % 360;
            }

            let rotatedViewport = page.getViewport({ scale: 1.0, rotation: pageRotation });
            const scale = Math.min(targetWidth / rotatedViewport.width, targetHeight / rotatedViewport.height);

            const scaledViewport = page.getViewport({ scale: scale, rotation: pageRotation });

            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(0, 0, targetWidth, targetHeight);

            const xOffset = (targetWidth - scaledViewport.width) / 2;
            const yOffset = (targetHeight - scaledViewport.height) / 2;

            const renderContext = {
                canvasContext: ctx,
                viewport: scaledViewport,
                transform: [1, 0, 0, 1, xOffset, yOffset]
            };

            await page.render(renderContext).promise;

            applyDitheringAndPack(ctx, kmbBytes, offset, targetWidth, targetHeight, bytesPerRow);

            offset += bytesPerPage;
        }

        progressBar.style.width = '95%';
        logMessage("PDF Conversion completed. Preparing upload...");

        let rawName = file.name.replace(/\.pdf$/i, '');
        let safeName = rawName
            .normalize("NFD").replace(/[\u0300-\u036f]/g, "")
            .replace(/[^a-zA-Z0-9_\-]/g, "_")
            + '.kmb';

        const kmbBlob = new Blob([kmbBuffer], { type: 'application/octet-stream' });
        await uploadKMB(kmbBlob, safeName, progressBar);

    } catch (err) {
        logMessage("Converter Error: " + err.message, true);
    }
}

// Core Rendering & Dithering Engine
function applyDitheringAndPack(ctx, kmbBytes, offset, width, height, bytesPerRow) {
    const imageData = ctx.getImageData(0, 0, width, height);
    const pixels = imageData.data;

    // Step 1: Grayscale conversion
    for (let i = 0; i < pixels.length; i += 4) {
        const luma = (pixels[i] * 0.299) + (pixels[i + 1] * 0.587) + (pixels[i + 2] * 0.114);
        pixels[i] = pixels[i + 1] = pixels[i + 2] = luma;
    }

    // Step 2: Floyd-Steinberg Dithering & Bit packing
    for (let py = 0; py < height; py++) {
        for (let px = 0; px < width; px++) {
            const pIdx = (py * width + px) * 4;
            const oldPixel = pixels[pIdx];

            const newPixel = oldPixel < 128 ? 0 : 255;
            pixels[pIdx] = newPixel;

            const quantError = oldPixel - newPixel;

            if (newPixel === 0) {
                const byteIdx = offset + (py * bytesPerRow) + Math.floor(px / 8);
                const bitIdx = 7 - (px % 8);
                kmbBytes[byteIdx] |= (1 << bitIdx);
            }

            if (px + 1 < width) pixels[pIdx + 4] += quantError * (7 / 16);
            if (py + 1 < height) {
                if (px - 1 >= 0) pixels[pIdx + (width * 4) - 4] += quantError * (3 / 16);
                pixels[pIdx + (width * 4)] += quantError * (5 / 16);
                if (px + 1 < width) pixels[pIdx + (width * 4) + 4] += quantError * (1 / 16);
            }
        }
    }
}

// Upload engine
function uploadKMB(blob, filename, bar) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        const formData = new FormData();
        formData.append('file', blob, filename);

        logMessage(`Starting transmission of ${filename} to KomaBon...`);

        xhr.upload.onprogress = e => {
            if (e.lengthComputable) {
                const pct = 95 + (e.loaded / e.total * 5);
                bar.style.width = pct + '%';
            }
        };

        xhr.onload = () => {
            if (xhr.status === 200) {
                bar.style.width = '100%';
                logMessage(`Upload completed successfully!`);
                if (typeof fetchBooks === "function") fetchBooks();
                resolve();
            } else {
                logMessage(`Upload failed. Server status: ${xhr.status}`, true);
                reject(new Error("Upload failed"));
            }
        };

        xhr.onerror = () => {
            logMessage(`Network error: KomaBon unreachable. (Running locally?)`, true);
            reject(new Error("Network error"));
        };

        xhr.open('POST', '/api/books/upload');
        xhr.send(formData);
    });
}