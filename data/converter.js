// Configure PDF.js worker
pdfjsLib.GlobalWorkerOptions.workerSrc = 'https://cdnjs.cloudflare.com/ajax/libs/pdf.js/3.11.174/pdf.worker.min.js';

let uploadHasError = false;
let lastFailedUpload = null;

// Virtual terminal logging
function logMessage(msg, isError = false) {
    const terminal = document.getElementById('terminal-log');
    const status = document.getElementById('comic-status');
    if (!terminal || !status) return;

    terminal.classList.remove('hidden');

    const line = document.createElement('div');
    const time = new Date().toLocaleTimeString();

    let logClass = "log-info";

    if (isError || msg.match(/error|failed|timeout|unreachable/i)) {
        logClass = "log-error";
        isError = true;
        uploadHasError = true;
    } else if (msg.match(/successfully|completed!|complete!/i)) {
        logClass = "log-success";
    } else if (msg.match(/warning|skipping/i)) {
        logClass = "log-warn";
    } else if (msg.match(/^--- /)) {
        logClass = "log-highlight";
    }

    line.className = logClass;
    line.innerText = `[${time}] ${msg}`;
    terminal.appendChild(line);
    terminal.scrollTop = terminal.scrollHeight;

    if (isError) {
        status.innerText = msg;
        status.style.color = "var(--danger)";
        terminal.style.borderColor = "var(--danger-line)";
    } else if (!uploadHasError) {
        status.innerText = msg;
        status.style.color = logClass === "log-success" ? "var(--success)" : "var(--accent)";

        if (logClass === "log-success") {
            terminal.style.borderColor = "var(--success)";
            setTimeout(() => { terminal.style.borderColor = "var(--line)"; }, 3000);
        }
    }
}

function resetTerminalState() {
    const terminal = document.getElementById('terminal-log');
    uploadHasError = false;
    lastFailedUpload = null;
    if (terminal) {
        terminal.innerHTML = '';
        terminal.style.borderColor = "var(--line)";
    }

    const retryContainer = document.getElementById('comic-retry-container');
    if (retryContainer) retryContainer.classList.add('hidden');
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

// --- Panel Focus & Smart Splitting Core ---

function togglePanelOptions() {
    const mode = document.getElementById('panel-mode')?.value;
    const container = document.getElementById('panel-options-container');
    if (!container) return;
    if (mode === 'panel_ai' || mode === 'panel_gutter') {
        container.classList.remove('hidden');
    } else {
        container.classList.add('hidden');
    }
}

const ONNX_CDN_URL = 'https://cdn.jsdelivr.net/npm/onnxruntime-web@1.17.1/dist/ort.all.min.js';
const ONNX_WASM_PATH = 'https://cdn.jsdelivr.net/npm/onnxruntime-web@1.17.1/dist/';
const AI_MODEL_URL = 'https://huggingface.co/mednasserallah/manga-panel-detector-yolo26n-onnx/resolve/main/manga_panel_detector_fp32_1024.onnx';
const AI_CACHE_NAME = 'komabon-ai-cache-v1';

let ortLoadedPromise = null;
let cachedAiSession = null;

function ensureOnnxRuntime() {
    if (typeof window !== 'undefined' && window.ort) {
        return Promise.resolve(window.ort);
    }
    if (ortLoadedPromise) return ortLoadedPromise;

    ortLoadedPromise = new Promise((resolve, reject) => {
        if (typeof document === 'undefined') {
            return reject(new Error("Document object is not available."));
        }
        const script = document.createElement('script');
        script.src = ONNX_CDN_URL;
        script.async = true;
        script.onload = () => {
            if (window.ort) {
                window.ort.env.wasm.wasmPaths = ONNX_WASM_PATH;
                resolve(window.ort);
            } else {
                reject(new Error("ONNX Runtime loaded but window.ort is undefined."));
            }
        };
        script.onerror = () => reject(new Error("Failed to load ONNX Runtime Web from CDN."));
        document.head.appendChild(script);
    });
    return ortLoadedPromise;
}

async function getOrFetchModelBuffer(url) {
    if (typeof window !== 'undefined' && 'caches' in window) {
        try {
            const cache = await caches.open(AI_CACHE_NAME);
            const cached = await cache.match(url);
            if (cached) {
                logMessage("AI panel detector loaded from browser cache.");
                return await cached.arrayBuffer();
            }
            logMessage("Downloading AI panel detector model (~9.6 MB, permanently cached)...");
            const resp = await fetch(url);
            if (!resp.ok) throw new Error(`Model download failed: HTTP ${resp.status}`);
            await cache.put(url, resp.clone());
            logMessage("AI panel detector model cached successfully.");
            return await resp.arrayBuffer();
        } catch (e) {
            logMessage(`Cache notice: ${e.message}. Attempting direct fetch...`);
        }
    }
    const resp = await fetch(url);
    if (!resp.ok) throw new Error(`Model fetch failed: HTTP ${resp.status}`);
    return await resp.arrayBuffer();
}

async function initAiPanelDetector() {
    if (cachedAiSession) return cachedAiSession;
    await ensureOnnxRuntime();
    const buffer = await getOrFetchModelBuffer(AI_MODEL_URL);

    logMessage("Initializing neural engine session...");
    const hasWebGPU = typeof navigator !== 'undefined' && 'gpu' in navigator;
    if (hasWebGPU) {
        try {
            cachedAiSession = await ort.InferenceSession.create(buffer, {
                executionProviders: ['webgpu', 'wasm'],
                graphOptimizationLevel: 'all'
            });
            logMessage("AI panel detector ready (WebGPU accelerated).");
            return cachedAiSession;
        } catch (err) {
            logMessage(`WebGPU note: ${err.message}. Using WebAssembly SIMD mode.`);
        }
    }

    cachedAiSession = await ort.InferenceSession.create(buffer, {
        executionProviders: ['wasm'],
        graphOptimizationLevel: 'all'
    });
    logMessage("AI panel detector ready (WASM SIMD mode).");
    return cachedAiSession;
}

// --- Bounding Box & Geometry Helpers ---

function boxArea(box) {
    return Math.max(0, box[2] - box[0]) * Math.max(0, box[3] - box[1]);
}

function overlapArea(a, b) {
    const ox1 = Math.max(a[0], b[0]);
    const oy1 = Math.max(a[1], b[1]);
    const ox2 = Math.min(a[2], b[2]);
    const oy2 = Math.min(a[3], b[3]);
    return Math.max(0, ox2 - ox1) * Math.max(0, oy2 - oy1);
}

function unionBox(a, b) {
    return [
        Math.min(a[0], b[0]),
        Math.min(a[1], b[1]),
        Math.max(a[2], b[2]),
        Math.max(a[3], b[3])
    ];
}

function isSliverPanel(box, pageW, pageH) {
    const w = Math.max(1, box[2] - box[0]);
    const h = Math.max(1, box[3] - box[1]);
    const areaFrac = (w * h) / Math.max(1, pageW * pageH);
    const aspect = Math.max(w / h, h / w);
    return (areaFrac < 0.025 && aspect > 4.0) || areaFrac < 0.025;
}

function isFullPagePanel(box, pageW, pageH, threshold = 0.92) {
    const w = Math.max(1, box[2] - box[0]);
    const h = Math.max(1, box[3] - box[1]);
    return (w / Math.max(1, pageW)) >= threshold && (h / Math.max(1, pageH)) >= threshold;
}

function dedupeBoxes(boxesWithConf, overlapThresh = 0.60) {
    const ordered = [...boxesWithConf].sort((a, b) => b.score - a.score);
    const kept = [];
    for (const item of ordered) {
        const box = item.box;
        const area = boxArea(box);
        if (area <= 0) continue;

        let mergedIdx = -1;
        for (let i = 0; i < kept.length; i++) {
            const kArea = boxArea(kept[i]);
            if (kArea > 0 && (overlapArea(box, kept[i]) / Math.min(area, kArea)) > overlapThresh) {
                mergedIdx = i;
                break;
            }
        }
        if (mergedIdx >= 0) {
            kept[mergedIdx] = unionBox(kept[mergedIdx], box);
        } else {
            kept.push(box);
        }
    }
    return kept;
}

function expandPanelsOverText(panels, texts, pageW, pageH) {
    if (!texts || texts.length === 0) return panels;

    const pad = Math.max(6, Math.round(Math.min(pageW, pageH) * 0.02));
    const expanded = panels.map(p => [...p]);

    for (const text of texts) {
        const tArea = boxArea(text);
        if (tArea <= 0) continue;

        let bestOwner = -1;
        let bestOverlap = 0;
        for (let i = 0; i < panels.length; i++) {
            const o = overlapArea(panels[i], text);
            if (o > bestOverlap) {
                bestOverlap = o;
                bestOwner = i;
            }
        }

        // Ownership threshold: at least 25% of the text bubble must belong to the panel
        if (bestOwner >= 0 && bestOverlap >= tArea * 0.25) {
            const base = panels[bestOwner];
            const target = expanded[bestOwner];
            if (text[0] < base[0]) target[0] = Math.min(target[0], text[0] - pad);
            if (text[1] < base[1]) target[1] = Math.min(target[1], text[1] - pad);
            if (text[2] > base[2]) target[2] = Math.max(target[2], text[2] + pad);
            if (text[3] > base[3]) target[3] = Math.max(target[3], text[3] + pad);
        }
    }

    for (const b of expanded) {
        b[0] = Math.max(0, b[0]);
        b[1] = Math.max(0, b[1]);
        b[2] = Math.min(pageW, b[2]);
        b[3] = Math.min(pageH, b[3]);
    }
    return expanded;
}

function mergeSmallGaps(splits, minSize) {
    if (!splits || splits.length <= 2) return splits || [];
    const merged = [splits[0]];
    for (let i = 1; i < splits.length; i++) {
        if (splits[i] - merged[merged.length - 1] < minSize) {
            continue;
        }
        merged.push(splits[i]);
    }
    if (merged[merged.length - 1] !== splits[splits.length - 1]) {
        merged[merged.length - 1] = splits[splits.length - 1];
    }
    return merged;
}

function detectPanelsGrid(sourceCanvas) {
    const w = sourceCanvas.width;
    const h = sourceCanvas.height;
    const ctx = sourceCanvas.getContext('2d', { willReadFrequently: true });
    const imgData = ctx.getImageData(0, 0, w, h).data;

    const threshold = 215;
    const purity = 0.94;
    const minGutter = Math.max(6, Math.floor(h * 0.013));
    const minBandH = Math.max(Math.floor(h * 0.05), 60);
    const minBandW = Math.max(Math.floor(w * 0.06), 60);

    const isWhiteRow = (y) => {
        let white = 0;
        const total = Math.floor(w / 2);
        for (let x = 0; x < w; x += 2) {
            const idx = (y * w + x) * 4;
            const luma = (imgData[idx] * 0.299) + (imgData[idx + 1] * 0.587) + (imgData[idx + 2] * 0.114);
            if (luma > threshold) white++;
        }
        return white > total * purity;
    };

    const hSplits = [0];
    let inGutter = false;
    let gutterStart = 0;
    for (let y = 0; y < h; y++) {
        const whiteRow = isWhiteRow(y);
        if (whiteRow && !inGutter) {
            inGutter = true;
            gutterStart = y;
        } else if (!whiteRow && inGutter) {
            if (y - gutterStart >= minGutter) {
                hSplits.push(Math.floor((gutterStart + y) / 2));
            }
            inGutter = false;
        }
    }
    hSplits.push(h);
    const mergedHSplits = mergeSmallGaps(hSplits, minBandH);

    const panels = [];
    for (let b = 0; b < mergedHSplits.length - 1; b++) {
        const y1 = mergedHSplits[b];
        const y2 = mergedHSplits[b + 1];

        const isWhiteCol = (x) => {
            let white = 0;
            const total = Math.max(1, Math.floor((y2 - y1) / 2));
            for (let y = y1; y < y2; y += 2) {
                const idx = (y * w + x) * 4;
                const luma = (imgData[idx] * 0.299) + (imgData[idx + 1] * 0.587) + (imgData[idx + 2] * 0.114);
                if (luma > threshold) white++;
            }
            return white > total * purity;
        };

        const vSplits = [0];
        let inVGutter = false;
        let vGutterStart = 0;
        for (let x = 0; x < w; x++) {
            const whiteCol = isWhiteCol(x);
            if (whiteCol && !inVGutter) {
                inVGutter = true;
                vGutterStart = x;
            } else if (!whiteCol && inVGutter) {
                if (x - vGutterStart >= minGutter) {
                    vSplits.push(Math.floor((vGutterStart + x) / 2));
                }
                inVGutter = false;
            }
        }
        vSplits.push(w);
        const mergedVSplits = mergeSmallGaps(vSplits, minBandW);

        for (let c = 0; c < mergedVSplits.length - 1; c++) {
            const x1 = mergedVSplits[c];
            const x2 = mergedVSplits[c + 1];
            panels.push([x1, y1, x2, y2]);
        }
    }

    if (panels.length <= 1) {
        return [[0, 0, w, h]];
    }
    return panels;
}

function yOverlapFrac(a, b) {
    const overlap = Math.min(a[3], b[3]) - Math.max(a[1], b[1]);
    const minH = Math.min(a[3] - a[1], b[3] - b[1]);
    return Math.max(0.0, overlap) / Math.max(1, minH);
}

function sortPanelsReadingOrder(panels, isRTL = true) {
    const n = panels.length;
    if (n <= 1) return panels;

    const OVERLAP_THRESHOLD = 0.30;
    const edges = Array.from({ length: n }, () => []);
    const inDegree = new Array(n).fill(0);

    for (let i = 0; i < n; i++) {
        for (let j = 0; j < n; j++) {
            if (i === j) continue;
            const a = panels[i];
            const b = panels[j];
            const aCx = (a[0] + a[2]) / 2;
            const bCx = (b[0] + b[2]) / 2;
            const aCy = (a[1] + a[3]) / 2;
            const bCy = (b[1] + b[3]) / 2;

            let readsFirst = false;
            if (yOverlapFrac(a, b) > OVERLAP_THRESHOLD) {
                readsFirst = isRTL ? (aCx > bCx) : (aCx < bCx);
            } else {
                readsFirst = aCy < bCy;
            }

            if (readsFirst) {
                edges[i].push(j);
                inDegree[j]++;
            }
        }
    }

    const tieBreakKey = (i) => {
        const p = panels[i];
        const cx = (p[0] + p[2]) / 2;
        const cy = (p[1] + p[3]) / 2;
        return { cy, hx: isRTL ? -cx : cx };
    };

    const available = [];
    for (let i = 0; i < n; i++) {
        if (inDegree[i] === 0) available.push(i);
    }

    const result = [];
    while (available.length > 0) {
        available.sort((idxA, idxB) => {
            const keyA = tieBreakKey(idxA);
            const keyB = tieBreakKey(idxB);
            if (Math.abs(keyA.cy - keyB.cy) > 5) return keyA.cy - keyB.cy;
            return keyA.hx - keyB.hx;
        });

        const node = available.shift();
        result.push(node);

        for (const neighbor of edges[node]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] === 0) {
                available.push(neighbor);
            }
        }
    }

    if (result.length !== n) {
        return [...panels].sort((a, b) => a[1] - b[1]);
    }

    return result.map(i => panels[i]);
}

async function detectPanelsAI(sourceCanvas) {
    const session = await initAiPanelDetector();
    const origW = sourceCanvas.width;
    const origH = sourceCanvas.height;

    const inputDim = 1024;
    const scale = Math.min(inputDim / origW, inputDim / origH);
    const scaledW = Math.round(origW * scale);
    const scaledH = Math.round(origH * scale);
    const padX = Math.floor((inputDim - scaledW) / 2);
    const padY = Math.floor((inputDim - scaledH) / 2);

    const letterboxCanvas = document.createElement('canvas');
    letterboxCanvas.width = inputDim;
    letterboxCanvas.height = inputDim;
    const lCtx = letterboxCanvas.getContext('2d', { willReadFrequently: true });

    // Fill with letterbox padding color (standard YOLO 114 gray)
    lCtx.fillStyle = 'rgb(114, 114, 114)';
    lCtx.fillRect(0, 0, inputDim, inputDim);
    lCtx.drawImage(sourceCanvas, 0, 0, origW, origH, padX, padY, scaledW, scaledH);

    const imgData = lCtx.getImageData(0, 0, inputDim, inputDim).data;
    const planeSize = inputDim * inputDim;
    const floatData = new Float32Array(3 * planeSize);

    for (let i = 0; i < planeSize; i++) {
        const p = i * 4;
        floatData[i] = imgData[p] / 255.0;
        floatData[planeSize + i] = imgData[p + 1] / 255.0;
        floatData[2 * planeSize + i] = imgData[p + 2] / 255.0;
    }

    const inputName = session.inputNames[0];
    const tensor = new ort.Tensor('float32', floatData, [1, 3, inputDim, inputDim]);
    const results = await session.run({ [inputName]: tensor });
    const outputName = session.outputNames[0];
    const output = results[outputName];
    const dims = output.dims;
    const data = output.data;

    const panelCandidates = [];
    const textCandidates = [];

    // Model is Ultralytics YOLO26n end2end: dims [1, 300, 6]
    // where each box has: [x1, y1, x2, y2, score, class_id]
    // class_id 0 = frame/panel, 1 = text
    const numDetections = (dims.length === 3) ? (dims[1] === 6 ? dims[2] : dims[1]) : (dims[0] === 6 ? dims[1] : dims[0]);
    const isChannelFirst = (dims.length === 3 && dims[1] === 6) || (dims.length === 2 && dims[0] === 6);

    for (let i = 0; i < numDetections; i++) {
        const x1 = isChannelFirst ? data[0 * numDetections + i] : data[i * 6 + 0];
        const y1 = isChannelFirst ? data[1 * numDetections + i] : data[i * 6 + 1];
        const x2 = isChannelFirst ? data[2 * numDetections + i] : data[i * 6 + 2];
        const y2 = isChannelFirst ? data[3 * numDetections + i] : data[i * 6 + 3];
        const score = isChannelFirst ? data[4 * numDetections + i] : data[i * 6 + 4];
        const cls = Math.round(isChannelFirst ? data[5 * numDetections + i] : data[i * 6 + 5]);

        if (score < 0.35) continue;

        // Map back from letterbox 1024x1024 to source canvas coordinates
        const origX1 = Math.max(0, Math.min(origW, (x1 - padX) / scale));
        const origY1 = Math.max(0, Math.min(origH, (y1 - padY) / scale));
        const origX2 = Math.max(0, Math.min(origW, (x2 - padX) / scale));
        const origY2 = Math.max(0, Math.min(origH, (y2 - padY) / scale));

        const box = [Math.round(origX1), Math.round(origY1), Math.round(origX2), Math.round(origY2)];
        const bw = box[2] - box[0];
        const bh = box[3] - box[1];

        if (cls === 1) {
            // Text / Speech bubble: never a panel crop! Used exclusively to expand panels
            if (score >= 0.40 && bw >= 10 && bh >= 10) {
                textCandidates.push(box);
            }
        } else if (cls === 0) {
            // Panel frame
            if (isSliverPanel(box, origW, origH)) continue;
            if (bw < origW * 0.08 || bh < origH * 0.05) continue;
            panelCandidates.push({ box, score });
        }
    }

    if (panelCandidates.length === 0) {
        return detectPanelsGrid(sourceCanvas);
    }

    // Deduplicate overlapping candidate frames into unions
    let frames = dedupeBoxes(panelCandidates, 0.60);

    // Safeguard: Check page coverage
    const totalArea = origW * origH;
    const coveredArea = frames.reduce((sum, b) => sum + boxArea(b), 0);
    const coverFrac = coveredArea / totalArea;

    // If only one full-page panel found or coverage is too low, use conservative gutter fallback
    if (frames.length <= 1) {
        if (frames.length === 1 && isFullPagePanel(frames[0], origW, origH)) {
            const grid = detectPanelsGrid(sourceCanvas);
            return grid.length > 1 ? grid : [[0, 0, origW, origH]];
        }
        const grid = detectPanelsGrid(sourceCanvas);
        return grid.length > 1 ? grid : [[0, 0, origW, origH]];
    }

    if (coverFrac < 0.35) {
        const grid = detectPanelsGrid(sourceCanvas);
        if (grid.length > 1) return grid;
    }

    // Grow panel frames over speech bubbles that straddle borders so text is never sliced
    frames = expandPanelsOverText(frames, textCandidates, origW, origH);

    return frames;
}

function renderCroppedToPage(sourceCanvas, crop, targetWidth, targetHeight, bytesPerRow, rotateWide = false) {
    const pageCanvas = document.createElement('canvas');
    pageCanvas.width = targetWidth;
    pageCanvas.height = targetHeight;
    const ctx = pageCanvas.getContext('2d', { willReadFrequently: true });

    ctx.fillStyle = '#FFFFFF';
    ctx.fillRect(0, 0, targetWidth, targetHeight);

    if (rotateWide && crop.w > crop.h) {
        // Optional user setting: rotate wide panel 90 degrees to fill vertical display
        const scale = Math.min(targetHeight / crop.w, targetWidth / crop.h);
        const w = crop.w * scale;
        const h = crop.h * scale;

        ctx.save();
        ctx.translate(targetWidth / 2, targetHeight / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.drawImage(sourceCanvas, crop.x, crop.y, crop.w, crop.h, -w / 2, -h / 2, w, h);
        ctx.restore();
    } else {
        // Natural upright orientation (preserves reading direction and text orientation)
        const scale = Math.min(targetWidth / crop.w, targetHeight / crop.h);
        const w = Math.round(crop.w * scale);
        const h = Math.round(crop.h * scale);
        const x = Math.round((targetWidth - w) / 2);
        const y = Math.round((targetHeight - h) / 2);
        ctx.drawImage(sourceCanvas, crop.x, crop.y, crop.w, crop.h, x, y, w, h);
    }

    const pageBytes = new Uint8Array(bytesPerRow * targetHeight);
    applyDitheringAndPack(ctx, pageBytes, 0, targetWidth, targetHeight, bytesPerRow);
    return pageBytes;
}

async function extractPagePanels(tempCanvas, tempCtx, panelMode, isRTL, paddingPercent, includeOverview) {
    const panelsOut = [];
    const W = tempCanvas.width;
    const H = tempCanvas.height;
    const fullCrop = getCropBounds(tempCtx, W, H);

    if (panelMode === 'full') {
        panelsOut.push(fullCrop);
        return panelsOut;
    }

    if (includeOverview) {
        panelsOut.push(fullCrop);
    }

    let detected = [];
    if (panelMode === 'panel_ai') {
        try {
            detected = await detectPanelsAI(tempCanvas);
        } catch (aiErr) {
            logMessage(`AI detection note: ${aiErr.message}. Using gutter detection.`, true);
            detected = detectPanelsGrid(tempCanvas);
        }
    } else {
        detected = detectPanelsGrid(tempCanvas);
    }

    if (!detected || detected.length === 0) {
        if (!includeOverview) {
            panelsOut.push(fullCrop);
        }
        return panelsOut;
    }

    // If only 1 panel was detected and it covers almost full page, treat as full page
    if (detected.length === 1 && isFullPagePanel(detected[0], W, H)) {
        if (!includeOverview) {
            panelsOut.push(fullCrop);
        }
        return panelsOut;
    }

    // Sort in topological reading order (RTL for manga, LTR for western comic)
    const sorted = sortPanelsReadingOrder(detected, isRTL);

    for (const b of sorted) {
        const bw = b[2] - b[0];
        const bh = b[3] - b[1];
        const padX = bw * paddingPercent;
        const padY = bh * paddingPercent;
        const cx = Math.max(0, Math.floor(b[0] - padX));
        const cy = Math.max(0, Math.floor(b[1] - padY));
        const cw = Math.min(W - cx, Math.ceil(bw + padX * 2));
        const ch = Math.min(H - cy, Math.ceil(bh + padY * 2));

        if (cw > 20 && ch > 20) {
            panelsOut.push({ x: cx, y: cy, w: cw, h: ch });
        }
    }

    if (panelsOut.length === 0) {
        panelsOut.push(fullCrop);
    }

    return panelsOut;
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
    const fileList = droppedFiles || fileInput.files;

    resetTerminalState();

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
    const panelMode = document.getElementById('panel-mode')?.value || 'full';
    const isRTL = (document.getElementById('panel-order')?.value || 'rtl') === 'rtl';
    const paddingPercent = parseInt(document.getElementById('panel-padding')?.value || '3') / 100;
    const includeOverview = document.getElementById('panel-overview')?.checked || false;
    const rotateWide = document.getElementById('panel-rotate')?.checked || false;

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
        const modeLabel = panelMode === 'panel_ai' ? 'Panel Focus (AI)' : (panelMode === 'panel_gutter' ? 'Panel Focus (Gutter)' : 'Full Page');
        logMessage(`Found ${pageCount} pages. Mode: ${modeLabel}. Starting KMB conversion...`);

        let effectivePanelMode = panelMode;
        if (panelMode === 'panel_ai') {
            try {
                await initAiPanelDetector();
            } catch (aiInitErr) {
                logMessage(`AI model initialization failed (${aiInitErr.message}). Falling back to Smart Gutter detection.`, true);
                effectivePanelMode = 'panel_gutter';
            }
        }

        const coverLen = 3040; // 640 bytes thumb (60x80) + 2400 bytes main cover (120x160)
        const bytesPerRow = Math.ceil(targetWidth / 8);
        const bytesPerPage = bytesPerRow * targetHeight;

        // Generate and inject high-quality dual thumbnails from the first page (cover)
        logMessage("Generating high-quality dual thumbnails for KMB...");
        const firstImgData = await zip.file(imgFiles[0]).async("blob");
        const firstBitmap = await createImageBitmap(firstImgData);

        const thumbBlob = await createThumbBlob(firstBitmap);
        const mainCoverBlob = await createMainCoverBlob(firstBitmap);
        const thumbBuffer = await thumbBlob.arrayBuffer();
        const mainCoverBuffer = await mainCoverBlob.arrayBuffer();
        firstBitmap.close();

        const pageBuffers = [];

        for (let i = 0; i < pageCount; i++) {
            progressBar.style.width = `${5 + (i / pageCount * 85)}%`;
            logMessage(`Processing source page ${i + 1}/${pageCount}...`);

            const imgData = await zip.file(imgFiles[i]).async("blob");
            const bitmap = await createImageBitmap(imgData);

            const tempCanvas = document.createElement('canvas');
            tempCanvas.width = bitmap.width;
            tempCanvas.height = bitmap.height;
            const tempCtx = tempCanvas.getContext('2d', { willReadFrequently: true });
            tempCtx.fillStyle = '#FFFFFF';
            tempCtx.fillRect(0, 0, tempCanvas.width, tempCanvas.height);
            tempCtx.drawImage(bitmap, 0, 0);

            const crops = await extractPagePanels(tempCanvas, tempCtx, effectivePanelMode, isRTL, paddingPercent, includeOverview);
            for (const crop of crops) {
                pageBuffers.push(renderCroppedToPage(tempCanvas, crop, targetWidth, targetHeight, bytesPerRow, rotateWide));
            }

            bitmap.close();
        }

        const finalPageCount = pageBuffers.length;
        logMessage(`Conversion completed: generated ${finalPageCount} e-paper pages. Packing KMB...`);

        const totalSize = 16 + coverLen + (bytesPerPage * finalPageCount);
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
        kmbView.setUint16(10, finalPageCount, true);
        kmbView.setUint32(12, coverLen, true);

        kmbBytes.set(new Uint8Array(thumbBuffer), 16);
        kmbBytes.set(new Uint8Array(mainCoverBuffer), 16 + 640);

        let offset = 16 + coverLen;
        for (let p = 0; p < finalPageCount; p++) {
            kmbBytes.set(pageBuffers[p], offset);
            offset += bytesPerPage;
        }

        progressBar.style.width = '95%';
        logMessage("Packing complete. Preparing upload...");

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
    const panelMode = document.getElementById('panel-mode')?.value || 'full';
    const isRTL = (document.getElementById('panel-order')?.value || 'rtl') === 'rtl';
    const paddingPercent = parseInt(document.getElementById('panel-padding')?.value || '3') / 100;
    const includeOverview = document.getElementById('panel-overview')?.checked || false;
    const rotateWide = document.getElementById('panel-rotate')?.checked || false;

    const progressBar = document.getElementById('comic-progress-bar');
    const progressContainer = document.getElementById('comic-progress');

    progressContainer.classList.remove('hidden');
    progressBar.style.width = '5%';

    try {
        logMessage("Loading PDF document into memory...");
        const arrayBuffer = await file.arrayBuffer();
        const pdf = await pdfjsLib.getDocument({ data: arrayBuffer }).promise;

        const pageCount = pdf.numPages;
        const modeLabel = panelMode === 'panel_ai' ? 'Panel Focus (AI)' : (panelMode === 'panel_gutter' ? 'Panel Focus (Gutter)' : 'Full Page');
        logMessage(`PDF loaded. Found ${pageCount} pages. Mode: ${modeLabel}.`);

        let effectivePanelMode = panelMode;
        if (panelMode === 'panel_ai') {
            try {
                await initAiPanelDetector();
            } catch (aiInitErr) {
                logMessage(`AI model initialization failed (${aiInitErr.message}). Falling back to Smart Gutter detection.`, true);
                effectivePanelMode = 'panel_gutter';
            }
        }

        const coverLen = 3040; // 640 bytes thumb (60x80) + 2400 bytes main cover (120x160)
        const bytesPerRow = Math.ceil(targetWidth / 8);
        const bytesPerPage = bytesPerRow * targetHeight;

        // Generate high-quality dual thumbnails from the first PDF page
        logMessage("Generating high-quality dual thumbnails for PDF cover...");
        const firstPage = await pdf.getPage(1);
        const baseVp = firstPage.getViewport({ scale: 1.0 });
        const coverScale = Math.min(targetWidth / baseVp.width, targetHeight / baseVp.height) * 2.0;
        const coverVp = firstPage.getViewport({ scale: coverScale });

        const coverCanvas = document.createElement('canvas');
        coverCanvas.width = coverVp.width;
        coverCanvas.height = coverVp.height;
        const coverCtx = coverCanvas.getContext('2d', { willReadFrequently: true });
        coverCtx.fillStyle = '#FFFFFF';
        coverCtx.fillRect(0, 0, coverCanvas.width, coverCanvas.height);

        await firstPage.render({ canvasContext: coverCtx, viewport: coverVp }).promise;
        const firstBitmap = await createImageBitmap(coverCanvas);

        const thumbBlob = await createThumbBlob(firstBitmap);
        const mainCoverBlob = await createMainCoverBlob(firstBitmap);
        const thumbBuffer = await thumbBlob.arrayBuffer();
        const mainCoverBuffer = await mainCoverBlob.arrayBuffer();
        firstBitmap.close();

        const pageBuffers = [];

        for (let i = 1; i <= pageCount; i++) {
            progressBar.style.width = `${5 + (i / pageCount * 85)}%`;
            logMessage(`Rendering PDF page ${i}/${pageCount}...`);

            const page = await pdf.getPage(i);
            let baseViewport = page.getViewport({ scale: 1.0 });
            let pageRotation = baseViewport.rotation;

            if (effectivePanelMode === 'full' && baseViewport.width > baseViewport.height) {
                pageRotation = (pageRotation + 270) % 360;
            }

            let rotatedViewport = page.getViewport({ scale: 1.0, rotation: pageRotation });
            const baseScale = Math.max(1.5, Math.min(2048 / rotatedViewport.width, 2048 / rotatedViewport.height));
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

            const crops = await extractPagePanels(tempCanvas, tempCtx, effectivePanelMode, isRTL, paddingPercent, includeOverview);
            for (const crop of crops) {
                pageBuffers.push(renderCroppedToPage(tempCanvas, crop, targetWidth, targetHeight, bytesPerRow, rotateWide));
            }
        }

        const finalPageCount = pageBuffers.length;
        logMessage(`PDF conversion completed: generated ${finalPageCount} e-paper pages. Packing KMB...`);

        const totalSize = 16 + coverLen + (bytesPerPage * finalPageCount);
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
        kmbView.setUint16(10, finalPageCount, true);
        kmbView.setUint32(12, coverLen, true);

        kmbBytes.set(new Uint8Array(thumbBuffer), 16);
        kmbBytes.set(new Uint8Array(mainCoverBuffer), 16 + 640);

        let offset = 16 + coverLen;
        for (let p = 0; p < finalPageCount; p++) {
            kmbBytes.set(pageBuffers[p], offset);
            offset += bytesPerPage;
        }

        progressBar.style.width = '95%';
        logMessage("PDF Packing complete. Preparing upload...");

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

        const retryContainer = document.getElementById('comic-retry-container');
        if (retryContainer) retryContainer.classList.add('hidden');

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
                lastFailedUpload = null;
                const retryContainer = document.getElementById('comic-retry-container');
                if (retryContainer) retryContainer.classList.add('hidden');
                if (typeof fetchBooks === "function") fetchBooks();
                resolve();
            } else {
                logMessage(`Upload failed. Server status: ${xhr.status}`, true);
                setupRetryMechanism(blob, filename, bar);
                resolve();
            }
        };

        xhr.onerror = () => {
            logMessage(`Network error: KomaBon unreachable.`, true);
            setupRetryMechanism(blob, filename, bar);
            resolve();
        };

        xhr.open('POST', '/api/books/upload');
        xhr.send(formData);
    });
}

function setupRetryMechanism(blob, filename, bar) {
    lastFailedUpload = { blob, filename, bar };
    const retryContainer = document.getElementById('comic-retry-container');
    if (retryContainer) {
        retryContainer.classList.remove('hidden');
    }
}

function retryFailedUpload() {
    if (!lastFailedUpload) return;
    const retryContainer = document.getElementById('comic-retry-container');
    if (retryContainer) retryContainer.classList.add('hidden');

    uploadHasError = false;
    const terminal = document.getElementById('terminal-log');
    if (terminal) terminal.style.borderColor = "var(--line)";
    const status = document.getElementById('comic-status');
    if (status) {
        status.style.color = "var(--accent)";
        status.innerText = "Retrying transfer...";
    }

    uploadKMB(lastFailedUpload.blob, lastFailedUpload.filename, lastFailedUpload.bar);
}