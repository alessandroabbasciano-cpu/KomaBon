// Mobile navigation drawer toggle
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

// Tab switching controller
function showTab(tabId) {
    closeNav();
    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav-links li').forEach(el => el.classList.remove('active'));

    document.getElementById(tabId).classList.add('active');

    const navItems = ['library', 'settings'];
    const tabIndex = navItems.indexOf(tabId);
    if (tabIndex >= 0) {
        document.querySelectorAll('.nav-links li')[tabIndex].classList.add('active');
    }

    if (tabId === 'library') {
        if (typeof fetchBooks === 'function') fetchBooks();
    } else if (tabId === 'settings') {
        getReaderProgress();
        getWifiStatus();
        getDisplaySettings();
        getSleepSettings();
        checkScreensaverStatus();
    }
}

// Global text sanitation utilities
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function escapeAttr(text) {
    const map = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
    return String(text).replace(/[&<>"']/g, c => map[c]);
}

// System Status Polling
async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();

        document.getElementById('battery-val').innerText = data.battery + '%' + (data.charging ? ' ⚡' : '');
        document.getElementById('uptime-val').innerText = data.uptime;

        if (data.version) {
            document.getElementById('current-ver').innerText = data.version;
            document.getElementById('version-display').innerText = data.version;
        }

        // Convert KB to MB for a cleaner UI
        const freeMB = (data.freeSpace / (1024 * 1024)).toFixed(1);
        const totalMB = (data.totalSpace / (1024 * 1024)).toFixed(1);
        document.getElementById('freespace-val').innerText = freeMB + ' / ' + totalMB + ' MB';

        let voltageText = data.voltage.toFixed(2) + 'V';
        if (data.charging) {
            document.getElementById('header-voltage').style.color = 'var(--success)';
        } else {
            document.getElementById('header-voltage').style.color = '';
        }
        document.getElementById('header-voltage').innerText = voltageText;

        const sdErrorBadge = document.getElementById('sd-error-badge');
        if (sdErrorBadge) {
            if (data.totalSpace === 0 && data.freeSpace === 0) {
                sdErrorBadge.classList.remove('hidden');
            } else {
                sdErrorBadge.classList.add('hidden');
            }
        }

        const batIcon = document.getElementById('battery-icon');
        const level = parseInt(data.battery);

        let visualLevel = 0;
        if (level > 90) visualLevel = 100;
        else if (level > 70) visualLevel = 80;
        else if (level > 50) visualLevel = 60;
        else if (level > 30) visualLevel = 40;
        else if (level > 10) visualLevel = 20;
        else visualLevel = 0;

        batIcon.setAttribute('data-level', visualLevel);

        if (data.charging) {
            batIcon.classList.add('charging');
        } else {
            batIcon.classList.remove('charging');
        }

        // Check FAT32 cluster alignment diagnostics
        const clusterWarn = document.getElementById('sd-cluster-warning');
        if (clusterWarn) {
            if (data.sdClusterSize && !data.sdClusterOptimal) {
                const kb = Math.round(data.sdClusterSize / 1024);
                clusterWarn.textContent = `⚡ Note: MicroSD cluster size is ${kb} KB. Formatting with 32 KB clusters is recommended to improve manga reading speed.`;
                clusterWarn.classList.remove('hidden');
            } else {
                clusterWarn.classList.add('hidden');
            }
        }

        // Check crash log diagnostics
        const crashCard = document.getElementById('crash-log-card');
        if (crashCard) {
            if (data.hasCrashLog) {
                crashCard.classList.remove('hidden');
                fetchCrashLog();
            } else {
                crashCard.classList.add('hidden');
            }
        }
    } catch (e) {
        console.error("Failed to fetch status", e);
    }
}

async function fetchCrashLog() {
    try {
        const res = await fetch('/api/system/crash_log');
        const data = await res.json();
        const content = document.getElementById('crash-log-content');
        const reason = document.getElementById('crash-reset-reason');
        if (content && data.log) {
            content.textContent = data.log;
        }
        if (reason && data.lastResetReason) {
            reason.textContent = `Last Reset Reason: ${data.lastResetReason} (Boot #${data.bootCount})`;
        }
    } catch (e) {
        console.error("Failed to fetch crash log", e);
    }
}

async function clearCrashLog() {
    try {
        const res = await fetch('/api/system/crash_log/clear', { method: 'POST' });
        if (res.ok) {
            const crashCard = document.getElementById('crash-log-card');
            if (crashCard) crashCard.classList.add('hidden');
        }
    } catch (e) {
        console.error("Failed to clear crash log", e);
    }
}

// Firmware update handlers
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

// Reader Settings (Font, Refresh Interval)
function getReaderSettings() {
    fetch('/api/settings/reader')
        .then(response => response.json())
        .then(data => {
            if (data.refreshFrequency) document.getElementById('refresh-rate').value = data.refreshFrequency;
            if (data.fontSize) document.getElementById('font-size').value = data.fontSize;
            if (data.fontFamily !== undefined) document.getElementById('font-family').value = data.fontFamily;
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
        headers: { 'Content-Type': 'application/json' },
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

// Storage Recovery Handlers
async function remountSD() {
    const btn = document.getElementById('btn-remount-sd');
    const msg = document.getElementById('sd-remount-status');

    if (!btn || !msg) return;

    btn.disabled = true;
    btn.innerText = "Remounting...";
    msg.textContent = "Negotiating hardware SPI bus teardown...";
    msg.style.color = "var(--accent)";

    try {
        const res = await fetch('/api/system/sd_remount', { method: 'POST' });
        const data = await res.json();

        if (res.ok && data.status === "ok") {
            msg.textContent = "MicroSD bus remounted successfully.";
            msg.style.color = "var(--success)";

            // Force a refresh of the library view to show the newly mounted files
            if (typeof fetchBooks === 'function') {
                setTimeout(fetchBooks, 500);
            }
        } else {
            throw new Error(data.error || "Remount sequence failed");
        }
    } catch (error) {
        console.error("SD Remount Error:", error);
        msg.textContent = `Remount Failed: ${error.message}. Try power cycling the device.`;
        msg.style.color = "var(--danger)";
    } finally {
        btn.disabled = false;
        btn.innerText = "Remount SD Bus";
        setTimeout(() => {
            if (msg.style.color === "var(--success)") {
                msg.textContent = "";
            }
        }, 4000);
    }
}

// Sleep and Timeout Settings
function getSleepSettings() {
    fetch('/api/settings/sleep')
        .then(response => response.json())
        .then(data => {
            if (data.sleepTimeout !== undefined) document.getElementById('sleep-timeout').value = data.sleepTimeout;
            if (data.screenMode !== undefined) {
                const el = document.getElementById('sleep-screen-mode');
                if (el) el.value = data.screenMode;
            }
            if (data.sleepMessage !== undefined) document.getElementById('sleep-message').value = data.sleepMessage;
        })
        .catch(error => console.error('Error loading sleep settings:', error));
}

function saveSleepSettings() {
    const sleepTimeout = parseInt(document.getElementById('sleep-timeout').value);
    const screenModeEl = document.getElementById('sleep-screen-mode');
    const screenMode = screenModeEl ? parseInt(screenModeEl.value) : 0;
    const sleepMessage = document.getElementById('sleep-message').value;
    const statusDiv = document.getElementById('sleep-settings-status');

    fetch('/api/settings/sleep', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sleepTimeout: sleepTimeout, screenMode: screenMode, sleepMessage: sleepMessage }),
    })
        .then(response => response.json())
        .then(data => {
            if (data.status === 'ok') {
                statusDiv.textContent = "Settings saved!";
                statusDiv.style.color = "var(--success)";
                setTimeout(() => statusDiv.textContent = "", 3000);
            } else {
                statusDiv.textContent = "Error saving settings.";
                statusDiv.style.color = "var(--danger)";
            }
        })
        .catch(error => {
            console.error('Error saving sleep settings:', error);
            statusDiv.textContent = "Connection error.";
            statusDiv.style.color = "var(--danger)";
        });
}

// --- Custom Screensaver Management ---
let g_preparedScreensaverBlob = null;

function renderRawScreensaverToCanvas(canvas, arrayBuffer) {
    const bytes = new Uint8Array(arrayBuffer);
    const offset = (bytes.length === 48004) ? 4 : 0;
    const w = 480;
    const h = 800;

    // Full-resolution 1-bit render on an offscreen canvas
    const src = document.createElement('canvas');
    src.width = w;
    src.height = h;
    const sctx = src.getContext('2d');
    const imgData = sctx.createImageData(w, h);
    const data = imgData.data;
    const bytesPerRow = 60;

    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const byteIdx = offset + (y * bytesPerRow) + Math.floor(x / 8);
            const bitIdx = 7 - (x % 8);
            const isBlack = (bytes[byteIdx] & (1 << bitIdx)) !== 0;
            const pIdx = (y * w + x) * 4;
            const val = isBlack ? 0 : 255;
            data[pIdx] = val;
            data[pIdx + 1] = val;
            data[pIdx + 2] = val;
            data[pIdx + 3] = 255;
        }
    }
    sctx.putImageData(imgData, 0, 0);

    // Smooth downscale for the on-screen preview (300x500 shown at ~150px wide = crisp on hi-dpi)
    canvas.width = 300;
    canvas.height = 500;
    const ctx = canvas.getContext('2d');
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(src, 0, 0, w, h, 0, 0, canvas.width, canvas.height);
}

async function handleScreensaverFileSelect(files) {
    if (!files || files.length === 0) return;
    const file = files[0];
    const statusDiv = document.getElementById('screensaver-status');
    const canvas = document.getElementById('screensaver-preview-canvas');
    const emptyPreview = document.getElementById('screensaver-empty-preview');
    const uploadBtn = document.getElementById('upload-screensaver-btn');

    statusDiv.textContent = "Processing and dithering image...";
    statusDiv.style.color = "var(--accent)";

    try {
        const img = new Image();
        const objectUrl = URL.createObjectURL(file);

        await new Promise((resolve, reject) => {
            img.onload = () => resolve();
            img.onerror = () => reject(new Error("Failed to decode image file"));
            img.src = objectUrl;
        });
        URL.revokeObjectURL(objectUrl);

        const TARGET_W = 480;
        const TARGET_H = 800;

        const convCanvas = document.createElement('canvas');
        convCanvas.width = TARGET_W;
        convCanvas.height = TARGET_H;
        const ctx = convCanvas.getContext('2d', { willReadFrequently: true });

        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(0, 0, TARGET_W, TARGET_H);

        const scale = Math.min(TARGET_W / img.width, TARGET_H / img.height);
        const drawW = Math.round(img.width * scale);
        const drawH = Math.round(img.height * scale);
        const drawX = Math.round((TARGET_W - drawW) / 2);
        const drawY = Math.round((TARGET_H - drawH) / 2);

        ctx.drawImage(img, drawX, drawY, drawW, drawH);

        const bytesPerRow = 60;
        const buffer = new ArrayBuffer(4 + (bytesPerRow * TARGET_H));
        const view = new DataView(buffer);
        view.setUint16(0, TARGET_W, true);
        view.setUint16(2, TARGET_H, true);
        const bytes = new Uint8Array(buffer);

        if (typeof applyDitheringAndPack === 'function') {
            applyDitheringAndPack(ctx, bytes, 4, TARGET_W, TARGET_H, bytesPerRow);
        } else {
            const imgData = ctx.getImageData(0, 0, TARGET_W, TARGET_H);
            const d = imgData.data;
            for (let i = 0; i < d.length; i += 4) {
                const lum = (d[i] * 0.299) + (d[i + 1] * 0.587) + (d[i + 2] * 0.114);
                d[i] = d[i + 1] = d[i + 2] = lum;
            }
            for (let y = 0; y < TARGET_H; y++) {
                for (let x = 0; x < TARGET_W; x++) {
                    const idx = (y * TARGET_W + x) * 4;
                    const oldPx = d[idx];
                    const newPx = oldPx < 128 ? 0 : 255;
                    d[idx] = d[idx + 1] = d[idx + 2] = newPx;
                    const err = (oldPx - newPx) >> 3;
                    if (x + 1 < TARGET_W) { d[idx + 4] += err; d[idx + 5] += err; d[idx + 6] += err; }
                    if (x + 2 < TARGET_W) { d[idx + 8] += err; d[idx + 9] += err; d[idx + 10] += err; }
                    if (y + 1 < TARGET_H) {
                        if (x - 1 >= 0) { d[idx + (TARGET_W * 4) - 4] += err; d[idx + (TARGET_W * 4) - 3] += err; d[idx + (TARGET_W * 4) - 2] += err; }
                        d[idx + (TARGET_W * 4)] += err; d[idx + (TARGET_W * 4) + 1] += err; d[idx + (TARGET_W * 4) + 2] += err;
                        if (x + 1 < TARGET_W) { d[idx + (TARGET_W * 4) + 4] += err; d[idx + (TARGET_W * 4) + 5] += err; d[idx + (TARGET_W * 4) + 6] += err; }
                    }
                    if (y + 2 < TARGET_H) {
                        d[idx + (TARGET_W * 8)] += err; d[idx + (TARGET_W * 8) + 1] += err; d[idx + (TARGET_W * 8) + 2] += err;
                    }
                }
            }
            for (let y = 0; y < TARGET_H; y++) {
                for (let x = 0; x < TARGET_W; x++) {
                    const idx = (y * TARGET_W + x) * 4;
                    if (d[idx] === 0) {
                        const byteIdx = 4 + (y * bytesPerRow) + Math.floor(x / 8);
                        bytes[byteIdx] |= (1 << (7 - (x % 8)));
                    }
                }
            }
        }

        g_preparedScreensaverBlob = new Blob([buffer], { type: 'application/octet-stream' });

        renderRawScreensaverToCanvas(canvas, buffer);
        canvas.style.display = 'inline-block';
        if (emptyPreview) emptyPreview.style.display = 'none';
        if (uploadBtn) uploadBtn.disabled = false;

        statusDiv.textContent = `Preview ready (${file.name}). Click 'Apply Screensaver' to send to device.`;
        statusDiv.style.color = "var(--success)";
    } catch (err) {
        console.error("Screensaver conversion error:", err);
        statusDiv.textContent = `Conversion failed: ${err.message}`;
        statusDiv.style.color = "var(--danger)";
    }
}

async function uploadCustomScreensaver() {
    if (!g_preparedScreensaverBlob) return;
    const statusDiv = document.getElementById('screensaver-status');
    const uploadBtn = document.getElementById('upload-screensaver-btn');
    const deleteBtn = document.getElementById('delete-screensaver-btn');

    uploadBtn.disabled = true;
    statusDiv.textContent = "Uploading screensaver to device...";
    statusDiv.style.color = "var(--accent)";

    try {
        const formData = new FormData();
        formData.append("file", g_preparedScreensaverBlob, "screensaver.raw");

        const response = await fetch('/api/settings/screensaver/upload', {
            method: 'POST',
            body: formData
        });

        const data = await response.json();
        if (data.ok) {
            statusDiv.textContent = "Screensaver uploaded and enabled!";
            statusDiv.style.color = "var(--success)";
            if (deleteBtn) deleteBtn.style.display = 'inline-block';
            const screenModeEl = document.getElementById('sleep-screen-mode');
            if (screenModeEl) screenModeEl.value = '1';
        } else {
            throw new Error(data.error || "Upload failed");
        }
    } catch (err) {
        console.error("Upload screensaver error:", err);
        statusDiv.textContent = `Upload error: ${err.message}`;
        statusDiv.style.color = "var(--danger)";
        uploadBtn.disabled = false;
    }
}

async function deleteCustomScreensaver() {
    if (!confirm("Are you sure you want to remove the custom screensaver?")) return;
    const statusDiv = document.getElementById('screensaver-status');
    const deleteBtn = document.getElementById('delete-screensaver-btn');
    const uploadBtn = document.getElementById('upload-screensaver-btn');
    const canvas = document.getElementById('screensaver-preview-canvas');
    const emptyPreview = document.getElementById('screensaver-empty-preview');

    statusDiv.textContent = "Removing screensaver...";
    statusDiv.style.color = "var(--accent)";

    try {
        const response = await fetch('/api/settings/screensaver', { method: 'DELETE' });
        const data = await response.json();
        if (data.ok) {
            statusDiv.textContent = "Screensaver removed. Default sleep cover will be used.";
            statusDiv.style.color = "var(--success)";
            if (deleteBtn) deleteBtn.style.display = 'none';
            if (uploadBtn) uploadBtn.disabled = true;
            if (canvas) canvas.style.display = 'none';
            if (emptyPreview) emptyPreview.style.display = 'block';
            g_preparedScreensaverBlob = null;
            const screenModeEl = document.getElementById('sleep-screen-mode');
            if (screenModeEl && screenModeEl.value === '1') screenModeEl.value = '0';
        } else {
            throw new Error(data.error || "Failed to remove screensaver");
        }
    } catch (err) {
        console.error("Delete screensaver error:", err);
        statusDiv.textContent = `Error: ${err.message}`;
        statusDiv.style.color = "var(--danger)";
    }
}

async function checkScreensaverStatus() {
    const statusDiv = document.getElementById('screensaver-status');
    const canvas = document.getElementById('screensaver-preview-canvas');
    const emptyPreview = document.getElementById('screensaver-empty-preview');
    const deleteBtn = document.getElementById('delete-screensaver-btn');
    if (!canvas) return;

    try {
        const response = await fetch('/api/settings/screensaver');
        const data = await response.json();

        if (data.exists) {
            if (deleteBtn) deleteBtn.style.display = 'inline-block';
            const rawResp = await fetch('/api/settings/screensaver?raw=1');
            if (rawResp.ok) {
                const buf = await rawResp.arrayBuffer();
                renderRawScreensaverToCanvas(canvas, buf);
                canvas.style.display = 'inline-block';
                if (emptyPreview) emptyPreview.style.display = 'none';
                if (statusDiv) {
                    statusDiv.textContent = `Custom screensaver active (${(data.size / 1024).toFixed(1)} KB)`;
                    statusDiv.style.color = "var(--success)";
                }
            }
        } else {
            if (deleteBtn) deleteBtn.style.display = 'none';
            if (canvas) canvas.style.display = 'none';
            if (emptyPreview) emptyPreview.style.display = 'block';
        }
    } catch (err) {
        console.error("Check screensaver error:", err);
    }
}

// Display Orientation Configuration
function getDisplaySettings() {
    fetch('/api/settings/display')
        .then(response => response.json())
        .then(data => {
            if (data.rotation !== undefined) document.getElementById('display-rotation').value = data.rotation;
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

// Wi-Fi Connection and Scanning
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

// Toggle Wi-Fi Debug Mode for reading sessions
function toggleDebugWifi() {
    const btn = document.getElementById('debug-wifi-btn');
    const originalText = btn.innerText;
    btn.innerText = "Toggling...";

    fetch('/api/debug', { method: 'POST' })
        .then(response => response.text())
        .then(data => {
            if (data === "1") {
                btn.innerText = "Disable Wi-Fi Debug";
                btn.classList.remove('secondary');
                btn.classList.add('primary');
            } else {
                btn.innerText = "Enable Wi-Fi Debug";
                btn.classList.remove('primary');
                btn.classList.add('secondary');
            }
        })
        .catch(error => {
            console.error('Error toggling debug mode:', error);
            btn.innerText = "Error!";
            setTimeout(() => { btn.innerText = originalText; }, 2000);
        });
}

// Initialization on load
setInterval(fetchStatus, 5000);
fetchStatus();

// Automatically trigger library list load immediately on page start
if (typeof fetchBooks === 'function') {
    fetchBooks();
}

getReaderSettings();
getReaderProgress();
getSleepSettings();
getWifiStatus();
getDisplaySettings();
checkScreensaverStatus();