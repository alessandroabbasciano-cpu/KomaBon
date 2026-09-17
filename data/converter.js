// Configure PDF.js worker
pdfjsLib.GlobalWorkerOptions.workerSrc = 'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

// Virtual terminal logging
function logMessage(msg, isError = false) {
    const terminal = document.getElementById('terminal-log');
    const status = document.getElementById('comic-status');
    if (!terminal || !status) return;

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

// --- Image Processing Core ---

function getCropBounds(ctx, width, height) {
    const imageData = ctx.getImageData(0, 0, width, height);
    const data = imageData.data;
    let minX = width, minY = height, maxX = 0, maxY = 0;

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = (y * width + x) * 4;
            const luma = (data[idx] * 0.299) + (data[idx + 1] * 0.587) + (data[idx + 2] * 0.114);

            if (luma < 245) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (minX > maxX || minY > maxY) {
        return { x: 0, y: 0, w: width, h: height };
    }

    const padding = 4;
    minX = Math.max(0, minX - padding);
    minY = Math.max(0, minY - padding);
    maxX = Math.min(width - 1, maxX + padding);
    maxY = Math.min(height - 1, maxY + padding);

    return { x: minX, y: minY, w: maxX - minX + 1, h: maxY - minY + 1 };
}

function applyAtkinsonDithering(imageData, width, height) {
    const data = imageData.data;

    for (let i = 0; i < data.length; i += 4) {
        const luma = (data[i] * 0.299) + (data[i + 1] * 0.587) + (data[i + 2] * 0.114);
        data[i] = data[i + 1] = data[i + 2] = luma;
    }

    for (let py = 0; py < height; py++) {
        for (let px = 0; px < width; px++) {
            const pIdx = (py * width + px) * 4;
            const oldPixel = data[pIdx];
            const newPixel = oldPixel < 128 ? 0 : 255;

            data[pIdx] = data[pIdx + 1] = data[pIdx + 2] = newPixel;
            const err = (oldPixel - newPixel) >> 3;

            if (px + 1 < width) {
                data[pIdx + 4] += err;
                data[pIdx + 5] += err;
                data[pIdx + 6] += err;
            }
            if (px + 2 < width) {
                data[pIdx + 8] += err;
                data[pIdx + 9] += err;
                data[pIdx + 10] += err;
            }
            if (py + 1 < height) {
                if (px - 1 >= 0) {
                    data[pIdx + (width * 4) - 4] += err;
                    data[pIdx + (width * 4) - 3] += err;
                    data[pIdx + (width * 4) - 2] += err;
                }
                data[pIdx + (width * 4)] += err;
                data[pIdx + (width * 4) + 1] += err;
                data[pIdx + (width * 4) + 2] += err;

                if (px + 1 < width) {
                    data[pIdx + (width * 4) + 4] += err;
                    data[pIdx + (width * 4) + 5] += err;
                    data[pIdx + (width * 4) + 6] += err;
                }
            }
            if (py + 2 < height) {
                data[pIdx + (width * 8)] += err;
                data[pIdx + (width * 8) + 1] += err;
                data[pIdx + (width * 8) + 2] += err;
            }
        }
    }
}

function applyDitheringAndPack(ctx, kmbBytes, offset, width, height, bytesPerRow) {
    const imageData = ctx.getImageData(0, 0, width, height);
    applyAtkinsonDithering(imageData, width, height);
    const pixels = imageData.data;

    for (let py = 0; py < height; py++) {
        for (let px = 0; px < width; px++) {
            const pIdx = (py * width + px) * 4;
            if (pixels[pIdx] === 0) {
                const byteIdx = offset + (py * bytesPerRow) + Math.floor(px / 8);
                const bitIdx = 7 - (px % 8);
                kmbBytes[byteIdx] |= (1 << bitIdx);
            }
        }
    }
}

async function createRawImageBlob(bitmap, maxWidth, maxHeight) {
    let scale = Math.min(maxWidth / bitmap.width, maxHeight / bitmap.height);
    if (scale > 1.0) scale = 1.0;

    const finalWidth = Math.round(bitmap.width * scale);
    const finalHeight = Math.round(bitmap.height * scale);

    const canvas = document.createElement('canvas');
    canvas.width = finalWidth;
    canvas.height = finalHeight;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });

    ctx.fillStyle = '#FFFFFF';
    ctx.fillRect(0, 0, finalWidth, finalHeight);
    ctx.drawImage(bitmap, 0, 0, finalWidth, finalHeight);

    const bytesPerRow = Math.ceil(finalWidth / 8);
    const payloadSize = bytesPerRow * finalHeight;

    const buffer = new ArrayBuffer(4 + payloadSize);
    const view = new DataView(buffer);
    const bytes = new Uint8Array(buffer);

    view.setUint16(0, finalWidth, true);
    view.setUint16(2, finalHeight, true);

    applyDitheringAndPack(ctx, bytes, 4, finalWidth, finalHeight, bytesPerRow);

    return new Blob([buffer], { type: 'application/octet-stream' });
}

// 60x80 (640 bytes) for Library
async function createThumbBlob(bitmap) {
    const w = 60;
    const h = 80;
    const canvas = document.createElement('canvas');
    canvas.width = w; canvas.height = h;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });

    ctx.fillStyle = '#FFFFFF'; ctx.fillRect(0, 0, w, h);

    const scale = Math.min(w / bitmap.width, h / bitmap.height);
    const drawW = bitmap.width * scale; const drawH = bitmap.height * scale;
    const drawX = (w - drawW) / 2; const drawY = (h - drawH) / 2;

    ctx.drawImage(bitmap, drawX, drawY, drawW, drawH);
    const buffer = new ArrayBuffer(640);
    const bytes = new Uint8Array(buffer);
    applyDitheringAndPack(ctx, bytes, 0, w, h, Math.ceil(w / 8));
    return new Blob([buffer], { type: 'application/octet-stream' });
}

// 120x160 (2400 bytes) for Main Menu Hero Card
async function createMainCoverBlob(bitmap) {
    const w = 120;
    const h = 160;
    const canvas = document.createElement('canvas');
    canvas.width = w; canvas.height = h;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });

    ctx.fillStyle = '#FFFFFF'; ctx.fillRect(0, 0, w, h);

    const scale = Math.min(w / bitmap.width, h / bitmap.height);
    const drawW = bitmap.width * scale; const drawH = bitmap.height * scale;
    const drawX = (w - drawW) / 2; const drawY = (h - drawH) / 2;

    ctx.drawImage(bitmap, drawX, drawY, drawW, drawH);
    const buffer = new ArrayBuffer(2400);
    const bytes = new Uint8Array(buffer);
    applyDitheringAndPack(ctx, bytes, 0, w, h, Math.ceil(w / 8));
    return new Blob([buffer], { type: 'application/octet-stream' });
}

// --- Drag and Drop Handlers ---
document.addEventListener('DOMContentLoaded', () => {
    const setupDropzone = (zoneId, callback) => {
        const zone = document.getElementById(zoneId);
        if (!zone) return;

        ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
            zone.addEventListener(eventName, preventDefaults, false);
        });

        function preventDefaults(e) {
            e.preventDefault();
            e.stopPropagation();
        }

        ['dragenter', 'dragover'].forEach(eventName => {
            zone.addEventListener(eventName, () => zone.classList.add('dragover'), false);
        });

        ['dragleave', 'drop'].forEach(eventName => {
            zone.addEventListener(eventName, () => zone.classList.remove('dragover'), false);
        });

        zone.addEventListener('drop', (e) => {
            const files = e.dataTransfer.files;
            if (files.length > 0) callback(files);
        }, false);
    };

    setupDropzone('converter-dropzone', (files) => {
        processInputFiles(files);
    });

    setupDropzone('font-dropzone', (files) => {
        const fileInput = document.getElementById('book-file');
        const dataTransfer = new DataTransfer();
        for (let i = 0; i < files.length; i++) dataTransfer.items.add(files[i]);
        fileInput.files = dataTransfer.files;
        uploadBook();
    });
});

async function processInputFiles(droppedFiles = null) {
    const fileInput = document.getElementById('universal-file');
    const terminal = document.getElementById('terminal-log');
    const fileList = droppedFiles || fileInput.files;

    terminal.innerHTML = '';

    if (!fileList || fileList.length === 0) {
        logMessage("Error: Select or drop files to process.", true);
        return;
    }

    for (let i = 0; i < fileList.length; i++) {
        const file = fileList[i];
        const ext = file.name.split('.').pop().toLowerCase();

        logMessage(`--- Starting file ${i + 1} of ${fileList.length}: ${file.name} ---`);

        try {
            if (ext === 'cbz' || ext === 'zip') {
                await processArchive(file);
            } else if (ext === 'pdf') {
                await processPDF(file);
            } else if (ext === 'epub') {
                await optimizeEPUB(file);
            } else if (ext === 'odt' || ext === 'rtf') {
                await convertODTtoEPUB(file);
            } else {
                logMessage(`Unsupported extension: ${ext}`, true);
            }
        } catch (err) {
            logMessage(`Error on ${file.name}: ${err.message}`, true);
        }

        logMessage(`--- Finished ${file.name} ---`);
    }

    fileInput.value = '';
}

async function processArchive(file) {
    const targetWidth = parseInt(document.getElementById('eink-width').value);
    const targetHeight = parseInt(document.getElementById('eink-height').value);
    const progressBar = document.getElementById('comic-progress-bar');
    const progressContainer = document.getElementById('comic-progress');

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '2%';

    try {
        logMessage("Unzipping archive in memory...");
        const zip = await JSZip.loadAsync(file);

        const imgFiles = Object.keys(zip.files).filter(name =>
            name.match(/\.(jpg|jpeg|png)$/i) && !name.startsWith('__MACOSX')
        ).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));

        if (imgFiles.length === 0) {
            throw new Error("No JPG/PNG images found in the archive.");
        }

        const pageCount = imgFiles.length;
        logMessage(`Found ${pageCount} pages. Starting KMB conversion...`);

        const bytesPerRow = Math.ceil(targetWidth / 8);
        const bytesPerPage = bytesPerRow * targetHeight;
        const totalSize = 16 + (bytesPerPage * pageCount);

        const kmbBuffer = new ArrayBuffer(totalSize);
        const kmbView = new DataView(kmbBuffer);
        const kmbBytes = new Uint8Array(kmbBuffer);

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

        for (let i = 0; i < pageCount; i++) {
            progressBar.style.width = `${10 + (i / pageCount * 80)}%`;
            logMessage(`Processing page ${i + 1}/${pageCount}`);

            const imgData = await zip.file(imgFiles[i]).async("blob");
            const bitmap = await createImageBitmap(imgData);

            const tempCanvas = document.createElement('canvas');
            tempCanvas.width = bitmap.width;
            tempCanvas.height = bitmap.height;
            const tempCtx = tempCanvas.getContext('2d', { willReadFrequently: true });
            tempCtx.fillStyle = '#FFFFFF';
            tempCtx.fillRect(0, 0, tempCanvas.width, tempCanvas.height);
            tempCtx.drawImage(bitmap, 0, 0);

            const crop = getCropBounds(tempCtx, tempCanvas.width, tempCanvas.height);

            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(0, 0, targetWidth, targetHeight);

            if (crop.w > crop.h) {
                const scale = Math.min(targetHeight / crop.w, targetWidth / crop.h);
                const w = crop.w * scale;
                const h = crop.h * scale;

                ctx.save();
                ctx.translate(targetWidth / 2, targetHeight / 2);
                ctx.rotate(-Math.PI / 2);
                ctx.drawImage(tempCanvas, crop.x, crop.y, crop.w, crop.h, -w / 2, -h / 2, w, h);
                ctx.restore();
            } else {
                const scale = Math.min(targetWidth / crop.w, targetHeight / crop.h);
                const w = crop.w * scale;
                const h = crop.h * scale;
                const x = (targetWidth - w) / 2;
                const y = (targetHeight - h) / 2;
                ctx.drawImage(tempCanvas, crop.x, crop.y, crop.w, crop.h, x, y, w, h);
            }

            applyDitheringAndPack(ctx, kmbBytes, offset, targetWidth, targetHeight, bytesPerRow);

            offset += bytesPerPage;
            bitmap.close();
        }

        progressBar.style.width = '95%';
        logMessage("Conversion completed. Preparing upload...");

        let rawName = file.name.replace(/\.(zip|cbz)$/i, '');
        let safeName = rawName.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/[^a-zA-Z0-9_\-\s]/g, "") + '.kmb';

        const kmbBlob = new Blob([kmbBuffer], { type: 'application/octet-stream' });
        await uploadKMB(kmbBlob, safeName, progressBar);

    } catch (err) {
        logMessage("Converter Error: " + err.message, true);
    }
}

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

        const kmbBuffer = new ArrayBuffer(totalSize);
        const kmbView = new DataView(kmbBuffer);
        const kmbBytes = new Uint8Array(kmbBuffer);

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
            logMessage(`Rendering PDF page ${i}/${pageCount}...`);

            const page = await pdf.getPage(i);
            let baseViewport = page.getViewport({ scale: 1.0 });
            let pageRotation = baseViewport.rotation;

            if (baseViewport.width > baseViewport.height) {
                pageRotation = (pageRotation + 270) % 360;
            }

            let rotatedViewport = page.getViewport({ scale: 1.0, rotation: pageRotation });
            const baseScale = Math.min(targetWidth / rotatedViewport.width, targetHeight / rotatedViewport.height) * 2.0;
            const hiResViewport = page.getViewport({ scale: baseScale, rotation: pageRotation });

            const tempCanvas = document.createElement('canvas');
            tempCanvas.width = hiResViewport.width;
            tempCanvas.height = hiResViewport.height;
            const tempCtx = tempCanvas.getContext('2d', { willReadFrequently: true });

            tempCtx.fillStyle = '#FFFFFF';
            tempCtx.fillRect(0, 0, tempCanvas.width, tempCanvas.height);

            const renderContext = {
                canvasContext: tempCtx,
                viewport: hiResViewport
            };

            await page.render(renderContext).promise;
            const crop = getCropBounds(tempCtx, tempCanvas.width, tempCanvas.height);

            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(0, 0, targetWidth, targetHeight);

            const finalScale = Math.min(targetWidth / crop.w, targetHeight / crop.h);
            const w = crop.w * finalScale;
            const h = crop.h * finalScale;
            const x = (targetWidth - w) / 2;
            const y = (targetHeight - h) / 2;

            ctx.drawImage(tempCanvas, crop.x, crop.y, crop.w, crop.h, x, y, w, h);

            applyDitheringAndPack(ctx, kmbBytes, offset, targetWidth, targetHeight, bytesPerRow);
            offset += bytesPerPage;
        }

        progressBar.style.width = '95%';
        logMessage("PDF Conversion completed. Preparing upload...");

        let rawName = file.name.replace(/\.pdf$/i, '');
        let safeName = rawName.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/[^a-zA-Z0-9_\-\s]/g, "") + '.kmb';

        const kmbBlob = new Blob([kmbBuffer], { type: 'application/octet-stream' });
        await uploadKMB(kmbBlob, safeName, progressBar);

    } catch (err) {
        logMessage("Converter Error: " + err.message, true);
    }
}

async function optimizeEPUB(file) {
    const targetWidth = parseInt(document.getElementById('eink-width').value);
    const targetHeight = parseInt(document.getElementById('eink-height').value);
    const progressBar = document.getElementById('comic-progress-bar');
    const progressContainer = document.getElementById('comic-progress');

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '5%';

    logMessage(`Extracting metadata and converting EPUB images to RAW: ${file.name}...`);

    try {
        const zip = await JSZip.loadAsync(file);

        let author = "Unknown";
        let title = "Unknown";
        let coverHref = "";

        try {
            const containerXml = await zip.file("META-INF/container.xml").async("text");
            const parser = new DOMParser();
            const containerDoc = parser.parseFromString(containerXml, "application/xml");
            const rootfiles = containerDoc.getElementsByTagNameNS("*", "rootfile");
            if (rootfiles.length > 0) {
                const opfPath = rootfiles[0].getAttribute("full-path");
                const opfDir = opfPath.includes('/') ? opfPath.substring(0, opfPath.lastIndexOf('/') + 1) : "";
                const opfXml = await zip.file(opfPath).async("text");
                const opfDoc = parser.parseFromString(opfXml, "application/xml");

                const creatorNode = opfDoc.getElementsByTagNameNS("*", "creator")[0];
                if (creatorNode) author = creatorNode.textContent.trim();

                const titleNode = opfDoc.getElementsByTagNameNS("*", "title")[0];
                if (titleNode) title = titleNode.textContent.trim();

                let epub2CoverId = "";
                const metaTags = opfDoc.getElementsByTagNameNS("*", "meta");
                for (let i = 0; i < metaTags.length; i++) {
                    if (metaTags[i].getAttribute("name") === "cover") {
                        epub2CoverId = metaTags[i].getAttribute("content");
                        break;
                    }
                }

                const manifestItems = opfDoc.getElementsByTagNameNS("*", "item");
                for (let i = 0; i < manifestItems.length; i++) {
                    const item = manifestItems[i];
                    const id = item.getAttribute("id");
                    const href = item.getAttribute("href");
                    const properties = item.getAttribute("properties");

                    if (properties && properties.includes("cover-image")) {
                        coverHref = opfDir + href;
                        break;
                    } else if (epub2CoverId && id === epub2CoverId) {
                        coverHref = opfDir + href;
                        break;
                    }
                }
            }
        } catch (e) {
            console.warn("Metadata or cover detection from OPF failed", e);
        }

        author = author.replace(/-/g, " ").replace(/[^a-zA-Z0-9_\s]/gi, "").trim();
        title = title.replace(/-/g, " ").replace(/[^a-zA-Z0-9_\s]/gi, "").trim();

        if (!author) author = "Unknown";
        if (!title) title = file.name.replace(/\.epub$/i, "");

        let safeName = `${author} - ${title}.epub`.replace(/\s+/g, " ");
        logMessage(`Resolved device filename: ${safeName}`);

        const imageFiles = Object.keys(zip.files).filter(name =>
            name.match(/\.(jpg|jpeg|png|gif|webp)$/i) && !name.startsWith('__MACOSX')
        );

        logMessage(`Found ${imageFiles.length} images. Generating RAW buffers...`);

        if (!coverHref || !zip.files[coverHref]) {
            const lowerImages = imageFiles.map(f => ({ path: f, lower: f.toLowerCase() }));
            const match = lowerImages.find(img =>
                img.lower.includes("cover") || img.lower.includes("copertina") || img.lower.includes("front") || img.lower.includes("titlepage")
            );
            coverHref = match ? match.path : imageFiles[0];
        }

        // Generate and inject BOTH covers (thumb & main) natively dithered
        if (coverHref && zip.files[coverHref]) {
            const coverData = await zip.file(coverHref).async("blob");
            const coverBitmap = await createImageBitmap(coverData);

            const thumbBlob = await createThumbBlob(coverBitmap);
            zip.file("cover_thumb.raw", thumbBlob);

            const mainCoverBlob = await createMainCoverBlob(coverBitmap);
            zip.file("cover_main.raw", mainCoverBlob);

            coverBitmap.close();
            logMessage(`Native Dual-Thumbnails successfully injected.`);
        }

        const fileReplacements = {};
        let processed = 0;

        for (let imgPath of imageFiles) {
            const imgData = await zip.file(imgPath).async("blob");
            const bitmap = await createImageBitmap(imgData);

            const rawBlob = await createRawImageBlob(bitmap, targetWidth, targetHeight);

            const newPath = imgPath.replace(/\.(jpg|jpeg|gif|png|webp)$/i, '.raw');
            if (newPath !== imgPath) {
                zip.remove(imgPath);
                fileReplacements[imgPath] = newPath;
            }
            zip.file(newPath, rawBlob);

            bitmap.close();
            processed++;
            progressBar.style.width = `${5 + (processed / imageFiles.length * 80)}%`;
        }

        if (Object.keys(fileReplacements).length > 0) {
            logMessage(`Updating internal EPUB references to .raw format...`);
            const textFiles = Object.keys(zip.files).filter(name => name.match(/\.(html|xhtml|opf|ncx)$/i));

            for (let path of textFiles) {
                let content = await zip.file(path).async("text");
                let changed = false;

                for (let oldPath in fileReplacements) {
                    const oldName = oldPath.split('/').pop();
                    const newName = fileReplacements[oldPath].split('/').pop();
                    if (content.includes(oldName)) {
                        content = content.split(oldName).join(newName);
                        changed = true;
                    }
                }

                if (changed) {
                    content = content.replace(/media-type="image\/(jpeg|png|gif)"/gi, 'media-type="application/octet-stream"');
                    zip.file(path, content);
                }
            }
        }

        logMessage(`Repackaging Zero-Decoding EPUB...`);
        const newEpubBlob = await zip.generateAsync({
            type: "blob",
            compression: "DEFLATE",
            compressionOptions: { level: 6 }
        });

        progressBar.style.width = '95%';
        logMessage(`Uploading to KomaBon...`);

        await uploadKMB(newEpubBlob, safeName, progressBar);

    } catch (err) {
        logMessage("EPUB Processing Error: " + err.message, true);
    }
}

async function convertODTtoEPUB(file) {
    const targetWidth = parseInt(document.getElementById('eink-width').value);
    const targetHeight = parseInt(document.getElementById('eink-height').value);
    const progressBar = document.getElementById('comic-progress-bar');
    const progressContainer = document.getElementById('comic-progress');

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '5%';

    logMessage(`Opening ODT archive: ${file.name}...`);

    try {
        const odtZip = await JSZip.loadAsync(file);
        const contentFile = odtZip.file("content.xml");
        if (!contentFile) throw new Error("Invalid ODT: content.xml not found.");

        const contentXmlStr = await contentFile.async("text");
        const parser = new DOMParser();
        const xmlDoc = parser.parseFromString(contentXmlStr, "application/xml");

        progressBar.style.width = '20%';
        logMessage(`Parsing ODT structure and compiling EPUB package...`);

        const epub = new JSZip();
        epub.file("mimetype", "application/epub+zip", { compression: "STORE" });

        epub.folder("META-INF").file("container.xml",
            `<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>`);

        const oebps = epub.folder("OEBPS");
        const textFolder = oebps.folder("Text");
        const imagesFolder = oebps.folder("Images");
        const stylesFolder = oebps.folder("Styles");

        stylesFolder.file("style.css", `
body { margin: 5%; text-align: justify; font-family: sans-serif; }
h1, h2, h3 { text-align: center; margin: 1em 0; }
p { margin: 0.5em 0; text-indent: 1.5em; }
img { max-width: 100%; height: auto; display: block; margin: 1em auto; }
`);

        const chaptersHtml = [];
        const manifestImages = [];
        let currentXhtml = "";
        let paragraphCount = 0;
        let imageCounter = 1;
        let isFirstImage = true;

        const bodyNode = xmlDoc.getElementsByTagNameNS("*", "body")[0]?.getElementsByTagNameNS("*", "text")[0];
        if (!bodyNode) throw new Error("Unable to locate document body in ODT.");

        async function extractTextRecursive(node) {
            if (!node || node.nodeType !== 1) return;

            const name = node.localName ? node.localName.toLowerCase() : node.nodeName.toLowerCase().replace(/^.*:/, '');

            if (name === "h") {
                const level = node.getAttributeNS("*", "outline-level") || node.getAttribute("text:outline-level") || "1";
                const headingTag = parseInt(level) <= 3 ? `h${level}` : "h3";
                const text = node.textContent.trim();

                if (text.length > 0) {
                    if (paragraphCount > 0) {
                        chaptersHtml.push(currentXhtml);
                        currentXhtml = "";
                        paragraphCount = 0;
                    }
                    currentXhtml += `<${headingTag}>${escapeHtml(text)}</${headingTag}>\n`;
                    paragraphCount++;
                }
            } else if (name === "p") {
                const drawImages = node.getElementsByTagNameNS("*", "image");
                for (let i = 0; i < drawImages.length; i++) {
                    const imgEl = drawImages[i];
                    const rawHref = imgEl.getAttributeNS("*", "href") || imgEl.getAttribute("xlink:href");

                    if (rawHref && odtZip.file(rawHref)) {
                        logMessage(`Converting ODT image to RAW: ${rawHref}`);
                        const imgBlob = await odtZip.file(rawHref).async("blob");
                        const bitmap = await createImageBitmap(imgBlob);

                        if (isFirstImage) {
                            const thumbBlob = await createThumbBlob(bitmap);
                            epub.file("cover_thumb.raw", thumbBlob);
                            const mainCoverBlob = await createMainCoverBlob(bitmap);
                            epub.file("cover_main.raw", mainCoverBlob);
                            isFirstImage = false;
                        }

                        const rawBinaryBlob = await createRawImageBlob(bitmap, targetWidth, targetHeight);
                        const imgFilename = `image_${imageCounter}.raw`;
                        imagesFolder.file(imgFilename, rawBinaryBlob);

                        manifestImages.push({ id: `img${imageCounter}`, filename: imgFilename });
                        currentXhtml += `<div class="img-wrapper"><img src="../Images/${imgFilename}" alt="Image" /></div>\n`;

                        imageCounter++;
                        bitmap.close();
                    }
                }

                const text = node.textContent.trim();
                if (text.length > 0) {
                    currentXhtml += `<p>${escapeHtml(text)}</p>\n`;
                    paragraphCount++;
                }

                if (paragraphCount >= 40) {
                    chaptersHtml.push(currentXhtml);
                    currentXhtml = "";
                    paragraphCount = 0;
                }
            } else {
                for (let i = 0; i < node.children.length; i++) {
                    await extractTextRecursive(node.children[i]);
                }
            }
        }

        await extractTextRecursive(bodyNode);

        if (currentXhtml.length > 0) {
            chaptersHtml.push(currentXhtml);
        }

        if (chaptersHtml.length === 0) {
            chaptersHtml.push("<p>Document contains no readable text.</p>");
        }

        const rawName = file.name.replace(/\.odt$/i, "");
        const safeTitle = rawName.replace(/-/g, " ").replace(/[^a-zA-Z0-9_\s]/gi, "").trim();
        const safeName = `Unknown - ${safeTitle}.epub`.replace(/\s+/g, " ");

        const bookTitle = safeTitle;

        let manifestItems = "";
        let spineItems = "";
        let navMapItems = "";

        for (let i = 0; i < chaptersHtml.length; i++) {
            const chapId = `chapter${i + 1}`;
            const chapFilename = `${chapId}.xhtml`;

            textFolder.file(chapFilename,
                `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>${escapeHtml(bookTitle)} - Part ${i + 1}</title>
  <link rel="stylesheet" type="text/css" href="../Styles/style.css"/>
</head>
<body>
${chaptersHtml[i]}
</body>
</html>`);

            manifestItems += `    <item id="${chapId}" href="Text/${chapFilename}" media-type="application/xhtml+xml"/>\n`;
            spineItems += `    <itemref idref="${chapId}"/>\n`;
            navMapItems += `    <navPoint id="navpoint-${i + 1}" playOrder="${i + 1}">
      <navLabel><text>Part ${i + 1}</text></navLabel>
      <content src="Text/${chapFilename}"/>
    </navPoint>\n`;
        }

        for (let img of manifestImages) {
            manifestItems += `    <item id="${img.id}" href="Images/${img.filename}" media-type="application/octet-stream"/>\n`;
        }

        oebps.file("content.opf",
            `<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookID" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>${escapeHtml(bookTitle)}</dc:title>
    <dc:language>it</dc:language>
    <dc:identifier id="BookID">urn:uuid:${Date.now()}</dc:identifier>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="style" href="Styles/style.css" media-type="text/css"/>
${manifestItems}  </manifest>
  <spine toc="ncx">
${spineItems}  </spine>
</package>`);

        oebps.file("toc.ncx",
            `<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.ncx.org/2005/ncx/" version="2005-1">
  <head>
    <meta name="dtb:uid" content="urn:uuid:${Date.now()}"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>${escapeHtml(bookTitle)}</text></docTitle>
  <navMap>
${navMapItems}  </navMap>
</ncx>`);

        progressBar.style.width = '85%';
        logMessage(`Compiling generated Zero-Decoding EPUB package...`);

        const newEpubBlob = await epub.generateAsync({
            type: "blob",
            compression: "DEFLATE",
            compressionOptions: { level: 6 }
        });

        progressBar.style.width = '95%';
        logMessage(`Uploading converted EPUB to KomaBon...`);

        await uploadKMB(newEpubBlob, safeName, progressBar);

    } catch (err) {
        logMessage("ODT Conversion Error: " + err.message, true);
    }
}

function uploadKMB(blob, filename, bar) {
    return new Promise((resolve, reject) => {
        const xhr = new XMLHttpRequest();
        const formData = new FormData();
        formData.append('file', blob, filename);

        logMessage(`Standard transmission of ${filename} to KomaBon...`);

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
            logMessage(`Network error: KomaBon unreachable.`, true);
            reject(new Error("Network error"));
        };

        xhr.open('POST', '/api/books/upload');
        xhr.send(formData);
    });
}