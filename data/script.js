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
    const navItems = ['dashboard', 'ereader', 'settings'];
    document.querySelectorAll('.nav-links li')[navItems.indexOf(tabId)].classList.add('active');

    // Load contextual data when entering specific tabs
    if (tabId === 'ereader') {
        if (typeof fetchBooks === 'function') fetchBooks();
        getReaderProgress();
    } else if (tabId === 'settings') {
        getWifiStatus();
        getDisplaySettings();
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
        document.getElementById('battery-val').innerText = data.battery + '%' + (data.charging ? ' (Charging)' : '');
        document.getElementById('uptime-val').innerText = data.uptime;

        if (data.version) {
            document.getElementById('current-ver').innerText = data.version;
            document.getElementById('version-display').innerText = data.version;
        }

        const freeKB = Math.round(data.freeSpace / 1024);
        const totalKB = Math.round(data.totalSpace / 1024);
        document.getElementById('freespace-val').innerText = freeKB + ' / ' + totalKB + ' KB';

        let voltageText = data.voltage.toFixed(2) + 'V';
        if (data.charging) {
            voltageText += ' ⚡';
            document.getElementById('header-voltage').style.color = '#00ff00';
        } else {
            document.getElementById('header-voltage').style.color = '';
        }
        document.getElementById('header-voltage').innerText = voltageText;

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
    } catch (e) {
        console.error("Failed to fetch status", e);
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

// Sleep and Timeout Settings
function getSleepSettings() {
    fetch('/api/settings/sleep')
        .then(response => response.json())
        .then(data => {
            if (data.sleepTimeout !== undefined) document.getElementById('sleep-timeout').value = data.sleepTimeout;
            if (data.sleepMessage !== undefined) document.getElementById('sleep-message').value = data.sleepMessage;
        })
        .catch(error => console.error('Error loading sleep settings:', error));
}

function saveSleepSettings() {
    const sleepTimeout = parseInt(document.getElementById('sleep-timeout').value);
    const sleepMessage = document.getElementById('sleep-message').value;
    const statusDiv = document.getElementById('sleep-settings-status');

    fetch('/api/settings/sleep', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
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
            statusDiv.style.color = "red";
        });
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

// Initialization on load
setInterval(fetchStatus, 5000);
fetchStatus();
getReaderSettings();
getReaderProgress();
getSleepSettings();
getWifiStatus();
getDisplaySettings();