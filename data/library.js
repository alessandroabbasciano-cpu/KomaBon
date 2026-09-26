// Case-insensitive file type filters
const isEpub = f => f.toLowerCase().endsWith('.epub');
const isFont = f => f.toLowerCase().endsWith('.ttf');
const isKmb = f => f.toLowerCase().endsWith('.kmb');

const STORAGE_KEY_BOOKS = 'komaBonLibrary';
const CHUNK_SIZE = 30;
let currentBooks = [];
let activeRenderedItems = [];
let renderedChunkCount = 0;
let scrollObserver = null;
let saveOrderTimer = null;
let bookListBound = false;
let selectedBooks = new Set();

function onBookCheckChange(checkbox) {
    const filename = checkbox.dataset.filename;
    if (checkbox.checked) {
        selectedBooks.add(filename);
    } else {
        selectedBooks.delete(filename);
    }
    updateBulkBar();
}

function onSeriesCheckChange(checkbox, seriesName) {
    const group = Array.from(document.querySelectorAll('.series-group')).find(g => g.dataset.series === seriesName);
    if (!group) return;
    const checks = group.querySelectorAll('.book-select-check');
    checks.forEach(c => {
        c.checked = checkbox.checked;
        if (checkbox.checked) {
            selectedBooks.add(c.dataset.filename);
        } else {
            selectedBooks.delete(c.dataset.filename);
        }
    });
    updateBulkBar();
}

function updateBulkBar() {
    const bulkBar = document.getElementById('bulk-bar');
    const bulkCount = document.getElementById('bulk-count');
    if (!bulkBar) return;

    if (selectedBooks.size > 0) {
        bulkBar.classList.remove('hidden');
        if (bulkCount) {
            bulkCount.textContent = `${selectedBooks.size} selected`;
        }
    } else {
        bulkBar.classList.add('hidden');
    }
}

function clearBulkSelection() {
    selectedBooks.clear();
    document.querySelectorAll('.book-select-check, .series-select-check').forEach(c => c.checked = false);
    updateBulkBar();
}

async function executeBulkDelete() {
    if (selectedBooks.size === 0) return;
    const count = selectedBooks.size;
    if (!confirm(`Permanently delete ${count} selected file(s)?`)) return;

    const filenames = Array.from(selectedBooks);
    try {
        const res = await fetch('/api/library/bulk_delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ filenames })
        });
        if (res.ok) {
            selectedBooks.clear();
            updateBulkBar();
            fetchBooks();
        } else {
            alert('Failed to delete selected files.');
        }
    } catch (e) {
        alert('Connection error during deletion.');
        console.error('Bulk delete failed', e);
    }
}

// Fetch library from ESP32 with Stale-While-Revalidate caching
async function fetchBooks() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;

    // 1. Instant cache hydration from sessionStorage or memory
    if (!currentBooks.length) {
        try {
            const cached = sessionStorage.getItem(STORAGE_KEY_BOOKS);
            if (cached) {
                currentBooks = JSON.parse(cached);
                renderBooks();
            }
        } catch (e) {
            console.warn('Failed to parse cached library', e);
        }
    }

    // Only show "Loading..." if there is no cached data to display yet
    if (!currentBooks.length) {
        bookList.innerHTML = '<p>Loading...</p>';
    }

    selectedBooks.clear();
    updateBulkBar();

    // 2. Background revalidation with ESP32
    try {
        const res = await fetch('/api/books');
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        const freshBooks = data.books || [];

        const freshJson = JSON.stringify(freshBooks);
        const currentJson = JSON.stringify(currentBooks);

        if (freshJson !== currentJson) {
            currentBooks = freshBooks;
            try {
                sessionStorage.setItem(STORAGE_KEY_BOOKS, freshJson);
            } catch (err) {
                console.warn('Failed to save books to sessionStorage', err);
            }
            renderBooks();
        }
    } catch (e) {
        if (!currentBooks.length) {
            bookList.innerHTML = '<p class="error">Error loading books.</p>';
        }
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

function renderProgressBar(book) {
    if (!book || (!book.page && !book.percent)) {
        return '';
    }
    const percent = book.percent || 0;
    const isCompleted = percent >= 100;
    const badgeClass = isCompleted ? 'progress-completed' : 'progress-ongoing';

    let label = '';
    if (isCompleted) {
        label = '✓ Completed';
    } else if (book.totalPages > 0) {
        label = `Page ${book.page || 1}/${book.totalPages} (${percent}%)`;
    } else if (book.page > 0) {
        label = `Page ${book.page}`;
    }

    if (!label) return '';

    return `
    <div class="book-progress-wrap ${badgeClass}">
        <div class="progress-bar-bg">
            <div class="progress-bar-fill" style="width: ${percent > 0 ? percent : 5}%"></div>
        </div>
        <span class="progress-label">${label}</span>
    </div>`;
}

function renderBookItem(book, epubs) {
    const bookIsFont = isFont(book.filename);
    const bookIsKmb = isKmb(book.filename);
    const isGhost = !!book.missing;
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

    const progressHtml = renderProgressBar(book);
    const isChecked = selectedBooks.has(book.filename) ? 'checked' : '';
    const ghostBadge = isGhost ? '<span class="ghost-badge">File missing</span>' : '';
    const sizeHtml = isGhost ? '<span class="book-size ghost-size">Missing</span>' : `<span class="book-size">${Math.round(book.size / 1024)} KB</span>`;
    const dlBtn = isGhost ? `<button class="btn-order" disabled title="File missing from storage">DL</button>` : `<button class="btn-order" data-action="download" data-filename="${nameAttr}" title="Download File">DL</button>`;

    return `
    <div class="book-item ${isGhost ? 'ghost-node' : ''}" data-filename="${nameAttr}">
        <input type="checkbox" class="book-select-check" data-filename="${nameAttr}" ${isChecked} onchange="onBookCheckChange(this)" title="Select">
        ${orderBtns}
        <div class="book-info-col">
            <span class="book-title">${displayIcon}${escapeHtml(book.name)}${ghostBadge}</span>
            ${progressHtml}
        </div>
        ${sizeHtml}
        ${dlBtn}
        <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(book.name)}">Delete</button>
    </div>`;
}

function buildGroupedStructure(books) {
    const seriesCounts = {};
    const bookSeriesMap = new Map();

    books.forEach(b => {
        if (isKmb(b.filename)) {
            const info = parseSeriesInfo(b.filename);
            if (info) {
                seriesCounts[info.series] = (seriesCounts[info.series] || 0) + 1;
                bookSeriesMap.set(b.filename, info);
            }
        }
    });

    const renderedItems = [];
    const processedSeries = new Set();

    books.forEach(book => {
        const info = bookSeriesMap.get(book.filename);
        if (info && seriesCounts[info.series] >= 2) {
            if (!processedSeries.has(info.series)) {
                processedSeries.add(info.series);
                const seriesBooks = books.filter(b => {
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

    return renderedItems;
}

function renderItemHtml(item, epubs, isSearchActive = false) {
    if (item.type === 'single') {
        return renderBookItem(item.book, epubs);
    } else {
        const seriesNameAttr = escapeAttr(item.name);
        const totalMb = (item.totalSize / (1024 * 1024)).toFixed(1);
        const sizeStr = item.totalSize >= 1024 * 1024 ? `${totalMb} MB` : `${Math.round(item.totalSize / 1024)} KB`;

        const completedCount = item.books.filter(b => b.percent >= 100).length;
        const ongoingCount = item.books.filter(b => b.percent > 0 && b.percent < 100).length;
        const missingCount = item.books.filter(b => b.missing).length;
        let seriesProgressBadge = '';
        if (completedCount === item.books.length && item.books.length > 0) {
            seriesProgressBadge = `<span class="series-progress-badge completed">✓ Completed (${completedCount}/${item.books.length})</span>`;
        } else if (completedCount > 0 || ongoingCount > 0) {
            seriesProgressBadge = `<span class="series-progress-badge ongoing">${completedCount}/${item.books.length} read</span>`;
        }
        let missingBadge = '';
        if (missingCount > 0) {
            missingBadge = `<span class="ghost-badge">${missingCount} missing</span>`;
        }

        const nestedHtml = item.books.map(b => {
            const nameAttr = escapeAttr(b.filename);
            const progressHtml = renderProgressBar(b);
            const isChecked = selectedBooks.has(b.filename) ? 'checked' : '';
            const isGhost = !!b.missing;
            const ghostBadge = isGhost ? '<span class="ghost-badge">File missing</span>' : '';
            const sizeHtml = isGhost ? '<span class="book-size ghost-size">Missing</span>' : `<span class="book-size">${Math.round(b.size / 1024)} KB</span>`;
            const dlBtn = isGhost ? `<button class="btn-order" disabled title="File missing from storage">DL</button>` : `<button class="btn-order" data-action="download" data-filename="${nameAttr}" title="Download File">DL</button>`;

            return `
            <div class="book-item series-nested-item ${isGhost ? 'ghost-node' : ''}" data-filename="${nameAttr}">
                <input type="checkbox" class="book-select-check" data-filename="${nameAttr}" ${isChecked} onchange="onBookCheckChange(this)" title="Select">
                <div class="book-info-col">
                    <span class="book-title">🖼️ ${escapeHtml(b.name)}${ghostBadge}</span>
                    ${progressHtml}
                </div>
                ${sizeHtml}
                ${dlBtn}
                <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(b.name)}">Delete</button>
            </div>`;
        }).join('');

        const allChecked = item.books.length > 0 && item.books.every(b => selectedBooks.has(b.filename));
        const isOpen = isSearchActive ? 'open' : '';

        return `
        <details class="series-group" data-series="${seriesNameAttr}" ${isOpen}>
            <summary class="series-header">
                <input type="checkbox" class="series-select-check" data-series="${seriesNameAttr}" ${allChecked ? 'checked' : ''} title="Select all in series" onclick="event.stopPropagation()" onchange="onSeriesCheckChange(this, '${seriesNameAttr}')">
                <span class="series-title">📚 <strong>${escapeHtml(item.name)}</strong></span>
                <span class="series-badge">${item.books.length} volumes</span>
                ${seriesProgressBadge}
                ${missingBadge}
                <span class="book-size">${sizeStr}</span>
            </summary>
            <div class="series-items">
                ${nestedHtml}
            </div>
        </details>`;
    }
}

function appendNextChunk() {
    const bookList = document.getElementById('book-list');
    if (!bookList || renderedChunkCount >= activeRenderedItems.length) {
        removeScrollSentinel();
        return;
    }

    const start = renderedChunkCount;
    const end = Math.min(start + CHUNK_SIZE, activeRenderedItems.length);
    const chunk = activeRenderedItems.slice(start, end);
    renderedChunkCount = end;

    const searchInput = document.getElementById('book-search');
    const isSearchActive = !!(searchInput && searchInput.value.trim());
    const epubs = currentBooks.filter(b => isEpub(b.filename));

    const chunkHtml = chunk.map(item => renderItemHtml(item, epubs, isSearchActive)).join('');

    const sentinel = document.getElementById('scroll-sentinel');
    if (sentinel) {
        sentinel.insertAdjacentHTML('beforebegin', chunkHtml);
    } else {
        bookList.insertAdjacentHTML('beforeend', chunkHtml);
    }

    if (renderedChunkCount < activeRenderedItems.length) {
        ensureScrollSentinel();
    } else {
        removeScrollSentinel();
    }
}

function ensureScrollSentinel() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;

    let sentinel = document.getElementById('scroll-sentinel');
    if (!sentinel) {
        sentinel = document.createElement('div');
        sentinel.id = 'scroll-sentinel';
        sentinel.style.height = '10px';
        bookList.appendChild(sentinel);
    } else {
        bookList.appendChild(sentinel);
    }

    if (!scrollObserver) {
        scrollObserver = new IntersectionObserver((entries) => {
            entries.forEach(entry => {
                if (entry.isIntersecting) {
                    appendNextChunk();
                }
            });
        }, { rootMargin: '200px' });
    }

    scrollObserver.disconnect();
    scrollObserver.observe(sentinel);
}

function removeScrollSentinel() {
    if (scrollObserver) {
        scrollObserver.disconnect();
    }
    const sentinel = document.getElementById('scroll-sentinel');
    if (sentinel) {
        sentinel.remove();
    }
}

// Render book list items
function renderBooks() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;

    if (!currentBooks.length) {
        removeScrollSentinel();
        bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        return;
    }

    const searchInput = document.getElementById('book-search');
    const query = searchInput ? searchInput.value.toLowerCase().trim() : '';

    let filteredBooks = currentBooks;
    if (query) {
        filteredBooks = currentBooks.filter(b => {
            const name = (b.name || '').toLowerCase();
            const file = (b.filename || '').toLowerCase();
            return name.includes(query) || file.includes(query);
        });
    }

    if (!filteredBooks.length) {
        removeScrollSentinel();
        bookList.innerHTML = '<p class="hint">No books found matching search.</p>';
        return;
    }

    activeRenderedItems = buildGroupedStructure(filteredBooks);
    renderedChunkCount = 0;
    removeScrollSentinel();
    bookList.innerHTML = '';

    appendNextChunk();
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
    try {
        sessionStorage.setItem(STORAGE_KEY_BOOKS, JSON.stringify(currentBooks));
    } catch (e) {}

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

    executeDirectUpload(file);
}

let lastFailedLibraryFile = null;

function retryLibraryUpload() {
    if (!lastFailedLibraryFile) return;
    executeDirectUpload(lastFailedLibraryFile);
}

function executeDirectUpload(file) {
    const fileInput = document.getElementById('book-file');
    const status = document.getElementById('upload-status');
    const progressContainer = document.getElementById('upload-progress');
    const progressBar = document.getElementById('upload-progress-bar');
    const dropzoneBox = document.getElementById('font-dropzone');

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
            lastFailedLibraryFile = null;
            progressBar.style.width = '100%';
            progressBar.style.backgroundColor = "var(--success)";
            status.innerText = "Upload complete!";
            status.style.color = "var(--success)";
            if (fileInput) fileInput.value = '';

            setTimeout(() => {
                progressContainer.classList.add('hidden');
                progressBar.style.backgroundColor = "var(--primary)";
            }, 3000);

            fetchBooks();
        } else {
            lastFailedLibraryFile = file;
            progressBar.style.backgroundColor = "var(--danger-line)";
            status.innerHTML = `<span>Upload failed: ${xhr.responseText || 'Error'}</span> <button type="button" class="btn secondary btn-micro" style="margin-left:8px;" onclick="retryLibraryUpload()">Retry</button>`;
            status.style.color = "var(--danger)";
            if (dropzoneBox) dropzoneBox.style.borderColor = "var(--danger-line)";
        }
    });

    xhr.addEventListener('error', () => {
        lastFailedLibraryFile = file;
        progressBar.style.backgroundColor = "var(--danger-line)";
        status.innerHTML = `<span>Upload error (Network failure).</span> <button type="button" class="btn secondary btn-micro" style="margin-left:8px;" onclick="retryLibraryUpload()">Retry</button>`;
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
// Filter books based on search input (reactive in-memory search with chunked rendering)
function filterBooks() {
    renderBooks();
}