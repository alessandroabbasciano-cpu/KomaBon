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

// Render book list items
function renderBooks() {
    const bookList = document.getElementById('book-list');
    if (!bookList) return;

    if (!currentBooks.length) {
        bookList.innerHTML = '<p class="hint">No books uploaded yet.</p>';
        return;
    }

    const epubs = currentBooks.filter(b => isEpub(b.filename));

    bookList.innerHTML = currentBooks.map(book => {
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
        <div class="book-item">
            ${orderBtns}
            <span class="book-title">${displayIcon}${escapeHtml(book.name)}</span>
            <span class="book-size">${Math.round(book.size / 1024)} KB</span>
            <button class="btn-order" data-action="download" data-filename="${nameAttr}" title="Download File">DL</button>
            <button class="btn-delete" data-action="delete" data-filename="${nameAttr}" data-name="${escapeAttr(book.name)}">Delete</button>
        </div>
    `}).join('');

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

function moveBook(filename, dir) {
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

// Upload EPUB or TTF directly without conversion
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

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '0%';
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
            status.innerText = "Upload complete!";
            status.style.color = "var(--success)";
            fileInput.value = '';

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

    xhr.addEventListener('error', () => {
        progressContainer.classList.add('hidden');
        status.innerText = "Upload error.";
        status.style.color = "var(--danger)";
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
    const query = document.getElementById('book-search').value.toLowerCase();
    const items = document.querySelectorAll('.book-item');

    items.forEach(item => {
        const title = item.querySelector('.book-title').textContent.toLowerCase();
        if (title.includes(query)) {
            item.style.display = 'flex';
        } else {
            item.style.display = 'none';
        }
    });
}