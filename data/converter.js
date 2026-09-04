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

// Router for Universal Converter inputs
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
        } else if (['jpg', 'jpeg', 'png'].includes(ext)) {
            logMessage("Single image processing logic pending implementation.", true);
        } else {
            logMessage(`Unsupported extension: ${ext}`, true);
        }
    } catch (err) {
        logMessage("Converter Error: " + err.message, true);
    }
}

// Text document routing
async function processTextDocument(file, ext) {
    if (ext === 'epub') {
        await optimizeEPUB(file);
    } else if (ext === 'odt') {
        await convertODTtoEPUB(file);
    } else {
        logMessage(`Conversion from ${ext.toUpperCase()} to EPUB pending implementation.`, true);
    }
}

// CBZ and ZIP comic converter (generates raw .kmb)
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
        logMessage(`Found ${pageCount} pages. Starting conversion...`);

        const bytesPerRow = Math.ceil(targetWidth / 8);
        const bytesPerPage = bytesPerRow * targetHeight;
        const totalSize = 16 + (bytesPerPage * pageCount);

        logMessage(`Allocating KMB binary buffer: ${Math.round(totalSize / 1024)} KB.`);
        const kmbBuffer = new ArrayBuffer(totalSize);
        const kmbView = new DataView(kmbBuffer);
        const kmbBytes = new Uint8Array(kmbBuffer);

        // Header signature: KMB1
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
            logMessage(`Processing page ${i + 1}/${pageCount} (${imgFiles[i]})`);

            const imgData = await zip.file(imgFiles[i]).async("blob");
            const bitmap = await createImageBitmap(imgData);

            ctx.fillStyle = '#FFFFFF';
            ctx.fillRect(0, 0, targetWidth, targetHeight);

            // Rotate landscape splash pages 90 degrees counter-clockwise
            if (bitmap.width > bitmap.height) {
                const scale = Math.min(targetHeight / bitmap.width, targetWidth / bitmap.height);
                const w = bitmap.width * scale;
                const h = bitmap.height * scale;

                ctx.save();
                ctx.translate(targetWidth / 2, targetHeight / 2);
                ctx.rotate(-Math.PI / 2);
                ctx.drawImage(bitmap, -w / 2, -h / 2, w, h);
                ctx.restore();
            } else {
                const scale = Math.min(targetWidth / bitmap.width, targetHeight / bitmap.height);
                const w = bitmap.width * scale;
                const h = bitmap.height * scale;
                const x = (targetWidth - w) / 2;
                const y = (targetHeight - h) / 2;
                ctx.drawImage(bitmap, x, y, w, h);
            }

            applyDitheringAndPack(ctx, kmbBytes, offset, targetWidth, targetHeight, bytesPerRow);

            offset += bytesPerPage;
            bitmap.close();
        }

        progressBar.style.width = '95%';
        logMessage("Conversion completed. Preparing upload...");

        let rawName = file.name.replace(/\.(zip|cbz|pdf)$/i, '');
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

// Vector PDF parser (generates raw .kmb)
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

// EPUB Optimizer (maintains text reflow, scales and dithers images)
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
        const imageFiles = Object.keys(zip.files).filter(name =>
            name.match(/\.(jpg|jpeg|png|gif|webp)$/i)
        );

        logMessage(`Found ${imageFiles.length} images. Processing...`);

        let processed = 0;
        for (let imgPath of imageFiles) {
            const imgData = await zip.file(imgPath).async("blob");
            const bitmap = await createImageBitmap(imgData);

            let scale = Math.min(targetWidth / bitmap.width, targetHeight / bitmap.height);
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

            const imageData = ctx.getImageData(0, 0, finalWidth, finalHeight);
            const data = imageData.data;

            for (let i = 0; i < data.length; i += 4) {
                const luma = (data[i] * 0.299) + (data[i + 1] * 0.587) + (data[i + 2] * 0.114);
                data[i] = data[i + 1] = data[i + 2] = luma;
            }

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

// Convert OpenDocument Text (.odt) to valid standard .epub
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

        const manifestImages = [];
        let imageCounter = 1;
        let xhtmlBody = "";

        const bodyNodes = xmlDoc.getElementsByTagNameNS("*", "body")[0]?.getElementsByTagNameNS("*", "text")[0];
        if (!bodyNodes) throw new Error("Unable to locate document body in ODT.");

        const children = bodyNodes.children;
        const totalNodes = children.length;

        for (let i = 0; i < totalNodes; i++) {
            const node = children[i];
            const localName = node.localName;

            if (localName === "h") {
                const level = node.getAttributeNS("*", "outline-level") || "1";
                const headingTag = parseInt(level) <= 3 ? `h${level}` : "h3";
                const headingText = node.textContent.trim();
                if (headingText.length > 0) {
                    xhtmlBody += `<${headingTag}>${escapeHtml(headingText)}</${headingTag}>\n`;
                }
            } else if (localName === "p") {
                const drawImages = node.getElementsByTagNameNS("*", "image");
                if (drawImages.length > 0) {
                    for (let imgEl of drawImages) {
                        const rawHref = imgEl.getAttributeNS("*", "href") || imgEl.getAttribute("xlink:href");
                        if (rawHref && odtZip.file(rawHref)) {
                            logMessage(`Dithering ODT image: ${rawHref}`);
                            const imgBlob = await odtZip.file(rawHref).async("blob");
                            const bitmap = await createImageBitmap(imgBlob);

                            let scale = Math.min(targetWidth / bitmap.width, targetHeight / bitmap.height);
                            if (scale > 1.0) scale = 1.0;

                            const finalWidth = Math.round(bitmap.width * scale);
                            const finalHeight = Math.round(bitmap.height * scale);

                            const canvas = document.createElement("canvas");
                            canvas.width = finalWidth;
                            canvas.height = finalHeight;
                            const ctx = canvas.getContext("2d", { willReadFrequently: true });

                            ctx.fillStyle = "#FFFFFF";
                            ctx.fillRect(0, 0, finalWidth, finalHeight);
                            ctx.drawImage(bitmap, 0, 0, finalWidth, finalHeight);

                            const imgData = ctx.getImageData(0, 0, finalWidth, finalHeight);
                            const data = imgData.data;

                            for (let p = 0; p < data.length; p += 4) {
                                const luma = (data[p] * 0.299) + (data[p + 1] * 0.587) + (data[p + 2] * 0.114);
                                data[p] = data[p + 1] = data[p + 2] = luma;
                            }

                            for (let py = 0; py < finalHeight; py++) {
                                for (let px = 0; px < finalWidth; px++) {
                                    const pIdx = (py * finalWidth + px) * 4;
                                    const oldPixel = data[pIdx];
                                    const newPixel = oldPixel < 128 ? 0 : 255;
                                    data[pIdx] = data[pIdx + 1] = data[pIdx + 2] = newPixel;
                                    const err = oldPixel - newPixel;

                                    if (px + 1 < finalWidth) {
                                        data[pIdx + 4] += err * (7 / 16);
                                        data[pIdx + 5] += err * (7 / 16);
                                        data[pIdx + 6] += err * (7 / 16);
                                    }
                                    if (py + 1 < finalHeight) {
                                        if (px - 1 >= 0) {
                                            data[pIdx + (finalWidth * 4) - 4] += err * (3 / 16);
                                            data[pIdx + (finalWidth * 4) - 3] += err * (3 / 16);
                                            data[pIdx + (finalWidth * 4) - 2] += err * (3 / 16);
                                        }
                                        data[pIdx + (finalWidth * 4)] += err * (5 / 16);
                                        data[pIdx + (finalWidth * 4) + 1] += err * (5 / 16);
                                        data[pIdx + (finalWidth * 4) + 2] += err * (5 / 16);
                                        if (px + 1 < finalWidth) {
                                            data[pIdx + (finalWidth * 4) + 4] += err * (1 / 16);
                                            data[pIdx + (finalWidth * 4) + 5] += err * (1 / 16);
                                            data[pIdx + (finalWidth * 4) + 6] += err * (1 / 16);
                                        }
                                    }
                                }
                            }
                            ctx.putImageData(imgData, 0, 0);

                            const newJpgBlob = await new Promise(res => canvas.toBlob(res, "image/jpeg", 0.9));
                            const imgFilename = `image_${imageCounter}.jpg`;
                            imagesFolder.file(imgFilename, newJpgBlob);

                            manifestImages.push({ id: `img${imageCounter}`, filename: imgFilename });
                            xhtmlBody += `<div class="img-wrapper"><img src="../Images/${imgFilename}" alt="Image" /></div>\n`;

                            imageCounter++;
                            bitmap.close();
                        }
                    }
                }

                const paragraphText = node.textContent.trim();
                if (paragraphText.length > 0) {
                    xhtmlBody += `<p>${escapeHtml(paragraphText)}</p>\n`;
                }
            } else if (localName === "list") {
                xhtmlBody += `<ul>\n`;
                const listItems = node.getElementsByTagNameNS("*", "list-item");
                for (let li of listItems) {
                    const liText = li.textContent.trim();
                    if (liText.length > 0) {
                        xhtmlBody += `  <li>${escapeHtml(liText)}</li>\n`;
                    }
                }
                xhtmlBody += `</ul>\n`;
            }

            progressBar.style.width = `${20 + (i / totalNodes * 60)}%`;
        }

        const bookTitle = file.name.replace(/\.odt$/i, "").replace(/[_-]/g, " ");
        textFolder.file("chapter1.xhtml",
            `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>${escapeHtml(bookTitle)}</title>
  <link rel="stylesheet" type="text/css" href="../Styles/style.css"/>
</head>
<body>
${xhtmlBody}
</body>
</html>`);

        let imageManifestEntries = "";
        for (let img of manifestImages) {
            imageManifestEntries += `    <item id="${img.id}" href="Images/${img.filename}" media-type="image/jpeg"/>\n`;
        }

        oebps.file("content.opf",
            `<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookID" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>${escapeHtml(bookTitle)}</dc:title>
    <dc:language>en</dc:language>
    <dc:identifier id="BookID">urn:uuid:${Date.now()}</dc:identifier>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="style" href="Styles/style.css" media-type="text/css"/>
    <item id="chapter1" href="Text/chapter1.xhtml" media-type="application/xhtml+xml"/>
${imageManifestEntries}  </manifest>
  <spine toc="ncx">
    <itemref idref="chapter1"/>
  </spine>
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
    <navPoint id="navpoint-1" playOrder="1">
      <navLabel><text>Start</text></navLabel>
      <content src="Text/chapter1.xhtml"/>
    </navPoint>
  </navMap>
</ncx>`);

        progressBar.style.width = '85%';
        logMessage(`Compiling generated EPUB package...`);

        const newEpubBlob = await epub.generateAsync({
            type: "blob",
            compression: "DEFLATE",
            compressionOptions: { level: 6 }
        });

        progressBar.style.width = '95%';
        logMessage(`Uploading converted EPUB to KomaBon...`);

        const rawName = file.name.replace(/\.odt$/i, "");
        const safeName = rawName
            .normalize("NFD").replace(/[\u0300-\u036f]/g, "")
            .replace(/[^a-zA-Z0-9_\-]/g, "_")
            + ".epub";

        await uploadKMB(newEpubBlob, safeName, progressBar);

    } catch (err) {
        logMessage("ODT Conversion Error: " + err.message, true);
    }
}

// Grayscale conversion, Floyd-Steinberg dithering and 1-bit MSB packing
function applyDitheringAndPack(ctx, kmbBytes, offset, width, height, bytesPerRow) {
    const imageData = ctx.getImageData(0, 0, width, height);
    const pixels = imageData.data;

    for (let i = 0; i < pixels.length; i += 4) {
        const luma = (pixels[i] * 0.299) + (pixels[i + 1] * 0.587) + (pixels[i + 2] * 0.114);
        pixels[i] = pixels[i + 1] = pixels[i + 2] = luma;
    }

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

// Multipart upload transmission engine
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
            logMessage(`Network error: KomaBon unreachable.`, true);
            reject(new Error("Network error"));
        };

        xhr.open('POST', '/api/books/upload');
        xhr.send(formData);
    });
}