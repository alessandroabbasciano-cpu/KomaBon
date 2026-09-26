// Case-insensitive file type filters
const isEpub = f => f.toLowerCase().endsWith('.epub');
const isFont = f => f.toLowerCase().endsWith('.ttf');
const isKmb = f => f.toLowerCase().endsWith('.kmb');

let currentBooks = [];
let saveOrderTimer = null;
let bookListBound = false;

// Fetch library from ESP32
async function fetchBooks() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;
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

function parseSeriesInfo(filename) {
    const nameWithoutExt = filename.replace(/\.(kmb|epub|cbz|zip|pdf)$/i, '');
    const match = nameWithoutExt.match(/^(.*?)(?:[\s._-]+(?:v(?:ol(?:ume)?)?|c(?:h(?:apter)?)?|tome)?[\s._-]*(\d+)(?:[a-z]|\b).*)$/i);
    if (match) {
        const series = match[1].replace(/[\s._-]+$/, '').trim();
        const volNum = parseInt(match[2], 10);
        if (series.length >= 2) {
            return { series, volume: isNaN(volNum) ? 0 : volNum };
        }
    }
    return null;
}

function renderBookItem(book, epubs) {
    const bookIsFont = isFont(book.filename);
    const bookIsKmb = isKmb(book.filename);
    const nameAttr = escapeAttr(book.filename);

    let orderBtns = '';
    if (isEpub(book.filename) && epubs.length > 1) {
        const idx = epubs.indexOf(book);
        orderBtns = `
            <span class="order-btns">
                <button class="btn-order" ${idx === 0 ? 'disabled' : ''} data-action="move" data-dir="-1" data-filename="${nameAttr}" title="Move up">▲</button>
                <button class="btn-order" ${idx === epubs.length - 1 ? 'disabled' : ''} data-action="move" data-dir="1" data-filename="${nameAttr}" title="Move down">▼</button>
            </span>`;
    }

    let displayIcon = '📖 ';
    if (bookIsFont) displayIcon = '📂 [Font] ';
    if (bookIsKmb) displayIcon = '🖼️ [Comic] ';

    return `
    <div class="book-item" data-filename="${nameAttr}">
        ${orderBtns}
        <span class="book-title">${displayIcon}${escapeHtml(book.name)}</span>
        <span class="book-size">${Math.round(book.size / 1024)} KB</span>
        <button class="btn-order" data-action="download" data-filename="${nameAttr}" title="Download File">DL</button>
        <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(book.name)}">Delete</button>
    </div>`;
}

// Render book list items
function renderBooks() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;

    if (!currentBooks.length) {
        bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        return;
    }

    const epubs = currentBooks.filter(b => isEpub(b.filename));

    // 1. Detect series for manga/comics (.kmb)
    const seriesCounts = {};
    const bookSeriesMap = new Map();

    currentBooks.forEach(b => {
        if (isKmb(b.filename)) {
            const info = parseSeriesInfo(b.filename);
            if (info) {
                seriesCounts[info.series] = (seriesCounts[info.series] || 0) + 1;
                bookSeriesMap.set(b.filename, info);
            }
        }
    });

    // 2. Build grouped data structure
    const renderedItems = [];
    const processedSeries = new Set();

    currentBooks.forEach(book => {
        const info = bookSeriesMap.get(book.filename);
        if (info && seriesCounts[info.series] >= 2) {
            if (!processedSeries.has(info.series)) {
                processedSeries.add(info.series);
                const seriesBooks = currentBooks.filter(b => {
                    const s = bookSeriesMap.get(b.filename);
                    return s && s.series === info.series;
                });
                seriesBooks.sort((x, y) => {
                    const sx = bookSeriesMap.get(x.filename);
                    const sy = bookSeriesMap.get(y.filename);
                    return (sx ? sx.volume : 0) - (sy ? sy.volume : 0);
                });
                const totalSize = seriesBooks.reduce((acc, b) => acc + (b.size || 0), 0);
                renderedItems.push({
                    type: 'series',
                    name: info.series,
                    books: seriesBooks,
                    totalSize
                });
            }
        } else {
            renderedItems.push({
                type: 'single',
                book: book
            });
        }
    });

    // 3. Render HTML
    bookList.innerHTML = renderedItems.map(item => {
        if (item.type === 'single') {
            return renderBookItem(item.book, epubs);
        } else {
            const seriesNameAttr = escapeAttr(item.name);
            const totalMb = (item.totalSize / (1024 * 1024)).toFixed(1);
            const sizeStr = item.totalSize >= 1024 * 1024 ? `${totalMb} MB` : `${Math.round(item.totalSize / 1024)} KB`;

            const nestedHtml = item.books.map(b => {
                const nameAttr = escapeAttr(b.filename);
                return `
                <div class="book-item series-nested-item" data-filename="${nameAttr}">
                    <span class="book-title">🖼️ ${escapeHtml(b.name)}</span>
                    <span class="book-size">${Math.round(b.size / 1024)} KB</span>
                    <button class="btn-order" data-action="download" data-filename="${nameAttr}" title="Download File">DL</button>
                    <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(b.name)}">Delete</button>
                </div>`;
            }).join('');

            return `
            <details class="series-group" data-series="${seriesNameAttr}">
                <summary class="series-header">
                    <span class="series-title">📚 <strong>${escapeHtml(item.name)}</strong></span>
                    <span class="series-badge">${item.books.length} volumi</span>
                    <span class="book-size">${sizeStr}</span>
                </summary>
                <div class="series-items">
                    ${nestedHtml}
                </div>
            </details>`;
        }
    }).join('');

    bindBookListActions();
}

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
        } else if (btn.dataset.action === 'download') {
            window.location.href = '/api/books/download?name=' + encodeURIComponent(btn.dataset.filename);
        }
    });
    bookListBound = true;
}

function updateOrderButtons() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;
    const epubs = currentBooks.filter(b => isEpub(b.filename));
    if (epubs.length <= 1) return;

    const items = Array.from(bookList.querySelectorAll('.book-item'));
    epubs.forEach((book, idx) => {
        const item = items.find(el => el.dataset.filename === book.filename);
        if (!item) return;
        const upBtn = item.querySelector('button[data-action="move"][data-dir="-1"]');
        const downBtn = item.querySelector('button[data-action="move"][data-dir="1"]');
        if (upBtn) upBtn.disabled = (idx === 0);
        if (downBtn) downBtn.disabled = (idx === epubs.length - 1);
    });
}

function moveBook(filename, dir) {
    const epubIdxs = currentBooks
        .map((b, i) => isEpub(b.filename) ? i : -1)
        .filter(i => i >= 0);
    const pos = epubIdxs.findIndex(i => currentBooks[i].filename === filename);
    const target = pos + dir;
    if (pos < 0 || target < 0 || target >= epubIdxs.length) return;

    const a = epubIdxs[pos], b = epubIdxs[target];
    const targetFilename = currentBooks[b].filename;
    [currentBooks[a], currentBooks[b]] = [currentBooks[b], currentBooks[a]];

    const bookList = document.getElementById('book-list');
    if (bookList) {
        const items = Array.from(bookList.querySelectorAll('.book-item'));
        const itemA = items.find(el => el.dataset.filename === filename);
        const itemB = items.find(el => el.dataset.filename === targetFilename);

        if (itemA && itemB) {
            if (dir === -1) {
                bookList.insertBefore(itemA, itemB);
            } else {
                bookList.insertBefore(itemB, itemA);
            }
            updateOrderButtons();
        } else {
            renderBooks();
        }
    } else {
        renderBooks();
    }

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

// Upload EPUB or TTF directly without conversion
function uploadBook() {
    const fileInput = document.getElementById('book-file');
    const status = document.getElementById('upload-status');
    const progressContainer = document.getElementById('upload-progress');
    const progressBar = document.getElementById('upload-progress-bar');
    const dropzoneBox = document.getElementById('font-dropzone');

    if (!fileInput.files.length) {
        status.innerText = "Please select a file.";
        status.style.color = "var(--danger)";
        if (dropzoneBox) dropzoneBox.style.borderColor = "var(--danger-line)";
        return;
    }

    const file = fileInput.files[0];
    if (!isEpub(file.name) && !isFont(file.name)) {
        status.innerText = "Only .epub and .ttf files are supported.";
        status.style.color = "var(--danger)";
        if (dropzoneBox) dropzoneBox.style.borderColor = "var(--danger-line)";
        return;
    }

    if (dropzoneBox) dropzoneBox.style.borderColor = "";
    progressContainer.classList.remove('hidden');
    progressBar.style.width = '0%';
    progressBar.style.backgroundColor = "var(--primary)";
    status.innerText = "Uploading...";
    status.style.color = "var(--accent)";

    const formData = new FormData();
    formData.append('file', file);

    const xhr = new XMLHttpRequest();

    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percentComplete = (e.loaded / e.total) * 100;
            progressBar.style.width = percentComplete + '%';
            status.innerText = `Uploading... ${Math.round(percentComplete)}%`;
        }
    });

    xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
            progressBar.style.width = '100%';
            progressBar.style.backgroundColor = "var(--success)";
            status.innerText = "Upload complete!";
            status.style.color = "var(--success)";
            fileInput.value = '';

            setTimeout(() => {
                progressContainer.classList.add('hidden');
                progressBar.style.backgroundColor = "var(--primary)";
            }, 3000);

            fetchBooks();
        } else {
            progressBar.style.backgroundColor = "var(--danger-line)";
            status.innerText = "Upload failed: " + xhr.responseText;
            status.style.color = "var(--danger)";
            if (dropzoneBox) dropzoneBox.style.borderColor = "var(--danger-line)";
        }
    });

    xhr.addEventListener('error', () => {
        progressBar.style.backgroundColor = "var(--danger-line)";
        status.innerText = "Upload error (Network failure).";
        status.style.color = "var(--danger)";
        if (dropzoneBox) dropzoneBox.style.borderColor = "var(--danger-line)";
        console.error("Upload failed");
    });

    xhr.open('POST', '/api/books/upload');
    xhr.send(formData);
}

async function deleteBook(filename, displayName) {
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

// Export / Import library reading state
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
    input.value = '';

    const status = document.getElementById('library-state-status');
    status.style.color = '';

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
// Filter books based on search input
function filterBooks() {
    const searchInput = document.getElementById('book-search');
    if (!searchInput) return;
    const query = searchInput.value.toLowerCase().trim();

    // 1. Standalone books (not inside a series group)
    const standaloneItems = document.querySelectorAll('#book-list > .book-item');
    standaloneItems.forEach(item => {
        const titleEl = item.querySelector('.book-title');
        const title = titleEl ? titleEl.textContent.toLowerCase() : '';
        item.style.display = (!query || title.includes(query)) ? 'flex' : 'none';
    });

    // 2. Series groups
    const seriesGroups = document.querySelectorAll('.series-group');
    seriesGroups.forEach(group => {
        const seriesTitle = (group.dataset.series || '').toLowerCase();
        const nestedItems = group.querySelectorAll('.series-nested-item');
        let anyChildMatches = false;

        nestedItems.forEach(item => {
            const titleEl = item.querySelector('.book-title');
            const title = titleEl ? titleEl.textContent.toLowerCase() : '';
            const matches = !query || title.includes(query) || seriesTitle.includes(query);
            item.style.display = matches ? 'flex' : 'none';
            if (matches && query) anyChildMatches = true;
        });

        if (!query) {
            group.style.display = 'block';
            group.open = false;
        } else if (seriesTitle.includes(query) || anyChildMatches) {
            group.style.display = 'block';
            group.open = true;
        } else {
            group.style.display = 'none';
        }
    });
}