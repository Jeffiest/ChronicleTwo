// The page around the game (docs/WEB.md). It makes the page cross-origin isolated (sw.js), keeps the player's
// game data in the origin private file system under chronicletwo/, and starts the game with that directory as
// its game root, mounted at /opfs (port/web/src/storage.cpp): darkchronicle.js, drawing with WebGPU, or
// darkchronicle_gl.js, drawing with WebGL2 (chooseRenderer). Extra arguments for the game can be given as
// ?args=--headless+--frames+30, the renderer as ?renderer=gl or ?renderer=webgpu, and how many pixels the game
// draws to the CSS pixel as ?scale=2 (pixelRatio).
//
// What the player chooses is stored in chronicletwo/ as one of:
// - data.zip and data.json, as dc2data extract writes them on a PC (or a data/ folder and data.json);
// - disc.iso, an image of the PAL disc, or disc/ (DATA.HD4, DATA.DAT, SOUND.HD3, SOUND.DAT), which the game
//   extracts into data.zip and data.json when it first starts (port/web/src/firstrun.cpp). The player chooses
//   how (showUnpack): the whole disc before the game starts, or the game started as soon as it can be and the
//   rest unpacked behind it.
// Storing one removes the others. saves/, config.json and logs/ beside them are the game's own and stay.
//
// A server that offers the game data (tools/web/serve.py --data) serves data.zip and data.json at gamedata/; the
// page then plays with those, read over the network as the game needs them, and nothing need be stored but saves.
'use strict';

const $ = (id) => document.getElementById(id);
const ISOLATE_FLAG = 'ct-isolate-reload';
const HOME = 'chronicletwo';
// A disc comes first: the data beside it is what the game extracted from it.
const KINDS = ['disc.iso', 'disc', 'data.zip', 'data'];
const MARKER = 'data.json';
const SERVER_DATA = 'gamedata/';
// What the server offers, as {size}, or null; found once at startup.
let server = null;

function setStatus(text) {
    $('status').textContent = text;
}

// The games the site offers, as a row of links above the title. games.json beside the page lists them,
// [{"name": "Dark Cloud", "href": "../"}, ...], the one whose href is this page's folder being this game. A site
// with one game has no games.json, and the page no row.
async function showGames() {
    let games;
    try {
        const response = await fetch('games.json', {cache: 'no-cache'});
        if (!response.ok) {
            return;
        }
        games = await response.json();
    } catch {
        return;
    }
    if (!Array.isArray(games)) {
        return;
    }
    const here = new URL('./', location.href).href;
    const row = $('games');
    for (const game of games) {
        if (typeof game?.name !== 'string' || typeof game?.href !== 'string') {
            continue;
        }
        const target = new URL(game.href, location.href);
        if (target.protocol !== 'https:' && target.protocol !== 'http:') {
            continue;
        }
        const link = document.createElement('a');
        link.textContent = game.name;
        link.href = target.href;
        if (target.href === here) {
            link.setAttribute('aria-current', 'page');
        }
        row.append(link);
    }
    row.hidden = row.childElementCount < 2;
}

// The run's last lines, kept in the browser's storage as they come, so a tab the browser kills (Safari reloads
// one that crashed or ran out of memory) leaves them to read at the next visit, as "Previous run's log".
const LOG_KEY = 'ct-log';
const LOG_LINES = 400;
// How many lines the Log drawer holds.
const LOG_SHOWN = 4000;
const kept = [];
let keepTimer = 0;

function keepLog(now) {
    clearTimeout(keepTimer);
    keepTimer = 0;
    const save = () => {
        try {
            localStorage.setItem(LOG_KEY, kept.join('\n'));
        } catch {
            // Storage full or refused: the log is a help, not a need.
        }
    };
    if (now) {
        save();
    } else {
        keepTimer = setTimeout(save, 250);
    }
}

// With the game data from the page's server (tools/web/serve.py), the log's lines go to it as well, every two
// seconds, so a phone's run is read on the computer beside its requests. An answer other than the server's 204,
// or five sends that failed, ends it.
const REPORT_TO = 'client-log';
const REPORT_HELD = 2000;
let report = null;
let reportFailed = 0;

function reportLine(text) {
    if (report) {
        report.push(text);
        if (report.length > REPORT_HELD) {
            report.splice(0, report.length - REPORT_HELD);
        }
    }
}

async function sendReport() {
    if (!report) {
        return;
    }
    if (report.length > 0) {
        const lines = report.splice(0, report.length).join('\n');
        try {
            const answer = await fetch(REPORT_TO, {method: 'POST', body: lines, cache: 'no-store'});
            reportFailed = answer.status === 204 ? 0 : 5;
        } catch {
            reportFailed++;
        }
        if (reportFailed >= 5) {
            report = null;
            return;
        }
    }
    setTimeout(sendReport, 2000);
}

function startReport() {
    report = [];
    setTimeout(sendReport, 2000);
    // What is still held when the page goes, the lines before a crash among them.
    addEventListener('pagehide', () => {
        if (report && report.length > 0) {
            navigator.sendBeacon(REPORT_TO, report.splice(0, report.length).join('\n'));
        }
    });
}

// The game's rates, a line a second (--log-fps, which a reporting page asks for): to the server, not the drawer.
const RATES = /\bpresent: FPS\b/;

// The screen's refresh rate, counted from half a second of its frames while the page waits for Start; 0 until
// then. The game shows no more frames than the screen does (CT_WEB_REFRESH, port/src/main.cpp).
let refreshRate = 0;

function measureRefresh() {
    let first = 0;
    let frames = 0;
    const step = (now) => {
        if (first === 0) {
            first = now;
        } else {
            frames++;
        }
        if (now - first < 500) {
            requestAnimationFrame(step);
        } else {
            const measured = frames * 1000 / (now - first);
            // The rate a screen is sold with nearest the count; a count far from all of them (a page in the
            // background is held to a few frames a second) says nothing.
            const known = [60, 72, 75, 90, 120, 144, 165, 240];
            const nearest = known.reduce((a, b) => (Math.abs(b - measured) < Math.abs(a - measured) ? b : a));
            refreshRate = Math.abs(nearest - measured) < nearest * 0.08 ? nearest : 0;
        }
    };
    requestAnimationFrame(step);
}

// How long the server takes to answer reads like the game's: of one byte, of 128 KiB and of 1 MiB one after
// another, six at once, two with a pause before each, and two the browser's cache may keep. Each is given as
// the page saw it and, in brackets, as the browser's network clock did (Resource Timing: the wait for the
// answer's first byte + the rest of it), which a page busy starting the game does not stretch. Which of them
// are slow says where a phone's time goes: every answer late by the same wait whatever its size, only large
// ones, only those after a pause, or only one at a time.
async function probeNetwork() {
    const address = new URL(`${SERVER_DATA}data.zip`, location.href).href;
    const before = performance.getEntriesByName(address).length;
    let reads = 0;
    const read = async (length, cache = 'no-store') => {
        const from = ++reads * 50331648;
        const began = performance.now();
        const answer = await fetch(address, {headers: {Range: `bytes=${from}-${from + length - 1}`}, cache});
        await answer.arrayBuffer();
        return Math.round(performance.now() - began);
    };
    const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const parts = [];
    try {
        const each = async (name, count, length, wait, cache) => {
            const times = [];
            for (let i = 0; i < count; i++) {
                if (wait) {
                    await pause(wait);
                }
                times.push(await read(length, cache));
            }
            parts.push([name, times, count]);
        };
        await each('1 byte', 3, 1);
        await each('128 KiB', 3, 131072);
        await each('1 MiB', 2, 1048576);
        const began = performance.now();
        await Promise.all([0, 1, 2, 3, 4, 5].map(() => read(131072)));
        parts.push([`six of 128 KiB at once, ${Math.round(performance.now() - began)} in all`, [], 6]);
        await each('128 KiB after a pause of 0.3 s', 2, 131072, 300);
        await each("128 KiB the browser's cache may keep", 2, 131072, 0, 'default');
    } catch {
        parts.push(['failed', [], 0]);
    }
    await pause(100);
    const entries = performance.getEntriesByName(address).slice(before);
    let at = 0;
    let made = 0;
    const text = parts.map(([name, times, count]) => {
        const clock = entries.slice(at, at + count).map((entry) => {
            made += entry.connectEnd > entry.connectStart ? 1 : 0;
            return `${Math.round(entry.responseStart - entry.requestStart)}+${Math.round(entry.responseEnd - entry.responseStart)}`;
        });
        at += count;
        return `${name}: ${times.join(', ')}${times.length ? ' ' : ''}(${clock.join(', ')})`;
    });
    log(`network: ms for reads of ${text.join('; ')}`);
    log(`network: ${entries[0]?.nextHopProtocol || 'protocol unknown'}, ${made} of ${entries.length} reads made a ` +
        `connection`);
}

function log(text) {
    reportLine(text);
    if (window.ctLogging) {
        kept.push(text);
        if (kept.length > LOG_LINES) {
            kept.splice(0, kept.length - LOG_LINES);
        }
        // What precedes a crash is written at once; the rest a little later, in one go.
        keepLog(/error|abort|fail|nan|exception|lost/i.test(text));
    }
    // Each line is a node of its own: adding to the text as a whole copied all of it for every line, which a
    // phone felt in the game's frames.
    const pre = $('log');
    pre.append(text + '\n');
    while (pre.childNodes.length > LOG_SHOWN) {
        pre.firstChild.remove();
    }
    if (!$('drawer').hidden) {
        pre.scrollTop = pre.scrollHeight;
    }
    console.log(text);
}

function mib(bytes) {
    return `${(bytes / 1048576).toFixed(0)} MiB`;
}

// True once the page is isolated; false when it cannot be; null while it reloads under the worker.
async function isolate() {
    if (self.crossOriginIsolated) {
        sessionStorage.removeItem(ISOLATE_FLAG);
        return true;
    }
    if (!('serviceWorker' in navigator) || sessionStorage.getItem(ISOLATE_FLAG)) {
        return false;
    }
    await navigator.serviceWorker.register('sw.js');
    await navigator.serviceWorker.ready;
    sessionStorage.setItem(ISOLATE_FLAG, '1');
    location.reload();
    return null;
}

async function gameDirectory() {
    const root = await navigator.storage.getDirectory();
    return root.getDirectoryHandle(HOME, {create: true});
}

async function entry(directory, name) {
    try {
        return await directory.getFileHandle(name);
    } catch {
        try {
            return await directory.getDirectoryHandle(name);
        } catch {
            return null;
        }
    }
}

async function hasFile(directory) {
    for await (const handle of directory.values()) {
        if (handle.kind === 'file' || await hasFile(handle)) {
            return true;
        }
    }
    return false;
}

// What is stored, as {kind, size, marker}, or null; marker says whether data.json is there too.
async function stored() {
    const home = await gameDirectory();
    const marker = (await entry(home, MARKER)) !== null;
    for (const kind of KINDS) {
        const handle = await entry(home, kind);
        if (handle === null) {
            continue;
        }
        if (handle.kind === 'file') {
            const file = await handle.getFile();
            if (file.size > 0) {
                return {kind, size: file.size, marker};
            }
        } else if (await hasFile(handle)) {
            return {kind, size: 0, marker};
        }
    }
    return null;
}

async function removeAll() {
    const home = await gameDirectory();
    for (const kind of [...KINDS, MARKER]) {
        await home.removeEntry(kind, {recursive: true}).catch(() => {});
    }
}

// A copy goes a part at a time, each read and then written, so the page can say how far it is, and say so when
// the browser stops answering instead of leaving the bar standing still.
const kCopyPart = 8 * 1024 * 1024;
const kStallSeconds = 30;

// The promise, or an error once it has gone kStallSeconds without settling. Seconds count only while the page is
// on screen: a hidden page is paused, and its reads and writes with it.
function watched(promise, what) {
    return new Promise((resolve, reject) => {
        let seconds = 0;
        const timer = setInterval(() => {
            if (document.visibilityState === 'visible' && ++seconds >= kStallSeconds) {
                clearInterval(timer);
                reject(new Error(`the browser stopped ${what}`));
            }
        }, 1000);
        promise.then(resolve, reject).finally(() => clearInterval(timer));
    });
}

// A part of a chosen file. A phone's file picker hands over a file it reads through another app, and such a read
// can fail once and work the next time, so it is tried three times.
async function readPart(file, from, to) {
    for (let attempt = 1; ; ++attempt) {
        try {
            return await watched(file.slice(from, to).arrayBuffer(), 'reading the file');
        } catch (error) {
            if (attempt === 3) {
                throw error;
            }
            await new Promise((resolve) => setTimeout(resolve, 500));
        }
    }
}

// Copies files into the game's directory, each a [relative path, File] pair, with the progress bar and the status
// line showing bytes. createWritable writes to a swap file and commits on close, so a file cut short is never left
// in place. The screen is kept on meanwhile: a phone that sleeps pauses the page and the copy with it.
async function copyFiles(files, progress = $('progress')) {
    const home = await gameDirectory();
    const total = files.reduce((sum, [, file]) => sum + file.size, 0);
    progress.hidden = false;
    progress.max = total || 1;
    progress.value = 0;
    let copied = 0;
    const directories = new Map();
    keepAwake(true);
    try {
        for (const [path, file] of files) {
            const parts = path.split('/').filter(Boolean);
            let directory = home;
            let key = '';
            for (const part of parts.slice(0, -1)) {
                key += '/' + part;
                if (!directories.has(key)) {
                    directories.set(key, await directory.getDirectoryHandle(part, {create: true}));
                }
                directory = directories.get(key);
            }
            const writable = await (await directory.getFileHandle(parts.at(-1), {create: true})).createWritable();
            try {
                for (let from = 0; from < file.size; from += kCopyPart) {
                    const part = await readPart(file, from, Math.min(file.size, from + kCopyPart));
                    await watched(writable.write(part), "writing to the browser's storage");
                    copied += part.byteLength;
                    progress.value = copied;
                    setStatus(`Copying into this browser: ${mib(copied)} of ${mib(total)}. Keep this page open and on screen.`);
                }
                await writable.close();
            } catch (error) {
                await writable.abort().catch(() => {});
                throw error;
            }
        }
    } catch (error) {
        // What the browser reports as its room is a share of the whole device, not what is free, so it is not quoted.
        const why = error.name === 'QuotaExceededError'
            ? `the browser has no more room for it: the copy needs ${mib(total)} free on this device`
            : error.message ?? error;
        throw new Error(`${why} (stopped after ${mib(copied)} of ${mib(total)})`);
    } finally {
        keepAwake(false);
    }
    progress.hidden = true;
}

// What is wrong with a chosen disc image, or null. The game reads the European disc alone, and without this a
// wrong disc, or one a download cut short, is copied whole and found out only when the game starts.
async function discFault(file) {
    const kSector = 2048;
    const bytes = async (from, length) => new DataView(await file.slice(from, from + length).arrayBuffer());
    const text = (view, at, length) => String.fromCharCode(...new Uint8Array(view.buffer, at, length));
    const volume = await bytes(16 * kSector, kSector);
    if (volume.byteLength < kSector || text(volume, 1, 5) !== 'CD001') {
        return 'it is not a disc image';
    }
    // The root directory's record sits at 156 in the volume descriptor: where it is, then how long.
    const root = await bytes(volume.getUint32(158, true) * kSector, volume.getUint32(166, true));
    const names = new Set();
    for (let at = 0; at + 33 < root.byteLength;) {
        const length = root.getUint8(at);
        if (length === 0) {
            at = (Math.floor(at / kSector) + 1) * kSector;
            continue;
        }
        names.add(text(root, at + 33, root.getUint8(at + 32)).replace(/;\d+$/, '').toUpperCase());
        at += length;
    }
    if (!names.has('DATA.HD4') || !names.has('DATA.DAT')) {
        return 'it is not the European Dark Chronicle disc (it has no DATA.HD4)';
    }
    const whole = volume.getUint32(80, true) * kSector;
    if (file.size < whole) {
        return `the image is cut short: the file holds ${mib(file.size)} of the disc's ${mib(whole)}`;
    }
    return null;
}

async function storeDisc(file) {
    const fault = await discFault(file);
    if (fault) {
        throw new Error(`${fault}. Nothing was copied`);
    }
    await removeAll();
    await copyFiles([['disc.iso', file]]);
}

// Files chosen together: data.zip with data.json, or a disc image. When both are chosen the extraction wins:
// it is ready to play, where the disc would be extracted again in the page.
async function storeFiles(list) {
    const files = [...list];
    const named = (test) => files.find((file) => test(file.name.toLowerCase()));
    const iso = named((name) => name.endsWith('.iso'));
    const zip = named((name) => name.endsWith('.zip'));
    const json = named((name) => name === 'data.json');
    if (iso && !(zip && json)) {
        await storeDisc(iso);
        return;
    }
    if (!zip) {
        throw new Error('choose a .iso disc image, or data.zip together with data.json');
    }
    if (!json) {
        throw new Error('data.zip needs the data.json dc2data wrote beside it: choose both');
    }
    await removeAll();
    await copyFiles([['data.zip', zip], [MARKER, json]]);
}

// A folder: the disc's files, a PC extraction (data.zip and data.json), data/ beside data.json, or a disc image.
// Only what is used is copied, so a folder that also holds the disc image beside its extraction stores the
// extraction alone.
async function storeFolder(list) {
    const files = [...list].map((file) => [file.webkitRelativePath.split('/').slice(1), file]);
    if (files.length === 0) {
        throw new Error('the folder is empty');
    }
    const top = (name) => files.find(([parts]) => parts.length === 1 && parts[0].toUpperCase() === name.toUpperCase());
    const disc = ['DATA.HD4', 'DATA.DAT', 'SOUND.HD3', 'SOUND.DAT'].map(top);
    if (disc.every(Boolean)) {
        await removeAll();
        await copyFiles(disc.map(([parts, file]) => [`disc/${parts[0].toUpperCase()}`, file]));
        return;
    }
    const zip = top('data.zip');
    const json = top(MARKER);
    if (zip && json) {
        await removeAll();
        await copyFiles([['data.zip', zip[1]], [MARKER, json[1]]]);
        return;
    }
    const inner = files.filter(([parts]) => parts.length > 1 && parts[0].toLowerCase() === 'data');
    if (json && inner.length > 0) {
        await removeAll();
        await copyFiles([[MARKER, json[1]], ...inner.map(([parts, file]) => [['data', ...parts.slice(1)].join('/'), file])]);
        return;
    }
    const images = files.filter(([parts]) => parts.length === 1 && parts[0].toLowerCase().endsWith('.iso'));
    if (images.length === 1) {
        await storeDisc(images[0][1]);
        return;
    }
    if (images.length > 1) {
        throw new Error('the folder holds more than one .iso: choose the disc image itself');
    }
    throw new Error("the folder holds neither the disc's files, data.zip (or data/) with data.json, nor a .iso");
}

function describe(what) {
    switch (what.kind) {
        case 'data.zip':
            return `data.zip is stored (${mib(what.size)}).`;
        case 'data':
            return 'The extracted game data is stored.';
        case 'disc.iso':
            return `The disc image is stored (${mib(what.size)})${what.marker ? ', extracted' : ''}.`;
        default:
            return `The disc's files are stored${what.marker ? ', extracted' : ''}.`;
    }
}

// The game data the page's server offers, or null when it offers none.
async function serverData() {
    try {
        const [zip, json] = await Promise.all(['data.zip', MARKER].map((name) =>
            fetch(SERVER_DATA + name, {method: 'HEAD', cache: 'no-store'})));
        if (!zip.ok || !json.ok) {
            return null;
        }
        return {size: Number(zip.headers.get('Content-Length') ?? 0)};
    } catch {
        return null;
    }
}

// How fast this device computes: MiB a second through a CRC-32 written in JavaScript, the best of the passes that
// fit in a tenth of a second. The unpacking is the same kind of work (compression, checksums, pictures), so its
// time on another device is taken to be longer or shorter by this.
function deviceSpeed() {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; ++n) {
        let c = n;
        for (let k = 0; k < 8; ++k) {
            c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        }
        table[n] = c;
    }
    const data = new Uint8Array(1 << 20);
    for (let i = 0; i < data.length; ++i) {
        data[i] = (i * 2654435761) >>> 24;
    }
    let best = 0;
    const until = performance.now() + 100;
    do {
        const began = performance.now();
        let crc = 0xffffffff;
        for (let i = 0; i < data.length; ++i) {
            crc = table[(crc ^ data[i]) & 0xff] ^ (crc >>> 8);
        }
        best = Math.max(best, 1000 / Math.max(performance.now() - began, 0.01));
    } while (performance.now() < until);
    return best;
}

// What deviceSpeed gives on the PC the times below were measured on (desktop Chrome, 24 processors).
const kReferenceSpeed = 640;
// The part of a whole unpacking after which the game can start when the rest is unpacked as it plays: 43 s of
// 77 and 48 s of 85 there, rounded up.
const kPlayShare = 0.6;

// How long unpacking a disc takes here, in seconds: {whole, play}, play being the wait before the game starts
// when the rest is unpacked behind it. The whole disc took desktop Chrome 220 s with two conversions at a time,
// 122 s with four and 77 to 85 s with eight, which 373 / n + 34 follows; the game runs half the processors'
// worth, two to eight (firstrun.cpp, ExtractJobs).
let unpackEstimate = null;

function estimateUnpack() {
    if (!unpackEstimate) {
        const jobs = Math.min(Math.max(Math.floor((navigator.hardwareConcurrency || 4) / 2), 2), 8);
        const speed = deviceSpeed();
        const whole = (373 / jobs + 34) * kReferenceSpeed / speed;
        unpackEstimate = {whole, play: whole * kPlayShare, speed, jobs};
    }
    return unpackEstimate;
}

function about(seconds) {
    if (seconds < 45) {
        return 'under a minute';
    }
    const minutes = Math.round(seconds / 60);
    return minutes === 1 ? 'about a minute' : `about ${minutes} minutes`;
}

// The two ways to unpack a stored disc, each with the time it is expected to take on this device.
function showUnpack() {
    const {whole, play} = estimateUnpack();
    $('unpack-whole-note').textContent = `The game starts when all of it is unpacked, ${about(whole)} on this ` +
        'device by a rough estimate, and then plays with nothing left to do.';
    $('unpack-play-note').textContent = `The game starts after ${about(play)}, and the rest is unpacked behind it ` +
        `for ${about(whole - play)} more. Until that is done the game can pause for a moment when it needs something ` +
        'not unpacked yet, and the page has to stay open: closed sooner, it starts the unpacking again next time.';
    const memory = navigator.deviceMemory;
    $('unpack-memory').hidden = !(memory && memory < 8);
    if (memory && memory < 8) {
        $('unpack-memory').textContent = `This device reports ${memory} GiB of memory. Unpacking a disc takes ` +
            'about 2.3 GiB at its peak, and about 3 GiB with the game running beside it, so it may not finish ' +
            'here, the second way least of all. The data.zip and data.json a PC extracted play without unpacking.';
    }
}

async function refresh() {
    if (server) {
        $('pick').hidden = true;
        $('ready').hidden = false;
        $('unpack').hidden = true;
        $('start').hidden = false;
        $('forget').hidden = true;
        $('stored').textContent = `The game data comes from the computer serving this page (data.zip, ${mib(server.size)}).`;
        setStatus('Ready. The game reads the data over the network as it plays; saves stay in this browser.');
        return;
    }
    const what = await stored();
    $('pick').hidden = what !== null;
    $('ready').hidden = what === null;
    // A disc that is extracted is no longer read: the game plays from data.zip, and the disc only takes space.
    const spent = what !== null && what.marker && (what.kind === 'disc.iso' || what.kind === 'disc');
    $('forget-disc').hidden = !spent;
    if (spent) {
        $('forget-disc').textContent = what.kind === 'disc.iso'
            ? `Remove the disc image (frees ${mib(what.size)})` : "Remove the disc's files";
    }
    // A disc not unpacked yet: the two ways to unpack it stand in for Start.
    const packed = what !== null && !what.marker && (what.kind === 'disc.iso' || what.kind === 'disc');
    $('unpack').hidden = !packed;
    $('start').hidden = packed;
    if (what) {
        $('stored').textContent = describe(what);
        setStatus(packed ? 'Ready. Choose how to unpack the disc.' : 'Ready.');
        if (packed) {
            showUnpack();
        }
    } else {
        setStatus('No game data in this browser yet.');
    }
}

// The game's "extract: <percent> <stage>" lines, shown over the canvas while it extracts a disc.
function notice(text, fraction) {
    $('notice').hidden = text === null;
    $('notice-text').textContent = text ?? '';
    const bar = $('notice-progress');
    bar.hidden = fraction === undefined;
    if (fraction !== undefined) {
        bar.max = 1;
        bar.value = fraction;
    }
}

// A phone that sleeps stops the page, and a copy or an extraction with it, so the screen is kept on while one runs. The
// browser drops the lock when the page is hidden; it is asked for again when the page shows.
let busy = false;
let wakeLock = null;
let wakeLockAsked = false;

async function keepAwake(on) {
    busy = on;
    if (!on) {
        wakeLock?.release().catch(() => {});
        wakeLock = null;
        return;
    }
    if (wakeLock || wakeLockAsked || !navigator.wakeLock || document.visibilityState !== 'visible') {
        return;
    }
    wakeLockAsked = true;
    try {
        const lock = await navigator.wakeLock.request('screen');
        lock.addEventListener('release', () => {
            if (wakeLock === lock) {
                wakeLock = null;
            }
        });
        if (busy) {
            wakeLock = lock;
        } else {
            lock.release().catch(() => {});
        }
    } catch {
        // Refused (battery saver, no permission): the work still runs while the page is on screen.
    }
    wakeLockAsked = false;
}

document.addEventListener('visibilitychange', () => {
    if (busy) {
        keepAwake(true);
    }
});

// The rest of a disc being unpacked behind the game ("unpack: <percent>", then "unpack: done" or "unpack: failed:
// <why>", firstrun.cpp): a line in the corner, the screen kept on, and a question before the page is closed.
let unpacking = false;

function showUnpacking(text) {
    $('unpacking').hidden = text === null;
    $('unpacking').textContent = text ?? '';
}

window.addEventListener('beforeunload', (event) => {
    if (unpacking) {
        event.preventDefault();
        event.returnValue = '';
    }
});

function print(text) {
    const extract = /^extract: (\d+) (.*)$/.exec(text);
    if (extract) {
        keepAwake(true);
        notice(`${unpackChoice === 'play' ? 'Unpacking what the game needs to start' : 'Unpacking the game data'}, ` +
            `once. Keep this page open and on screen. ${extract[2]}`, Number(extract[1]) / 100);
        return;
    }
    const rest = /^unpack: (\d+)$/.exec(text);
    if (rest) {
        unpacking = true;
        keepAwake(true);
        showUnpacking(`Unpacking the rest of the disc: ${rest[1]}%. Keep this page open.`);
        return;
    }
    if (text === 'extract: done') {
        keepAwake(false);
        notice(null);
    } else if (text.startsWith('extract: ')) {
        keepAwake(!text.startsWith('extract: failed'));
        notice(text.slice('extract: '.length));
    } else if (text === 'unpack: done') {
        unpacking = false;
        keepAwake(false);
        showUnpacking('The disc is unpacked.');
        setTimeout(() => showUnpacking(null), 8000);
    } else if (text.startsWith('unpack: failed')) {
        unpacking = false;
        keepAwake(false);
        showUnpacking(null);
        notice(`Unpacking the disc stopped (${text.slice('unpack: failed: '.length)}). ` +
            'Reload the page to start it again.');
    }
    if (report && RATES.test(text)) {
        reportLine(text);
        return;
    }
    log(text);
}

// The on-screen gamepad on a touch screen (port/web/src/touch.cpp): its state goes to the game as a button mask
// in touch.cpp's order and two sticks from -1 to 1, y down; the game says when it shows.
const PAD_BITS = {up: 0, down: 1, left: 2, right: 3, cross: 4, circle: 5, square: 6, triangle: 7,
    l1: 8, r1: 9, l2: 10, r2: 11, select: 12, start: 13};
const TOUCH_SCREEN = matchMedia('(pointer: coarse)').matches;
const touches = new Map(); // pointer id -> {mask} or {stick, x, y}

function sendPad() {
    let mask = 0;
    const sticks = {left: [0, 0], right: [0, 0]};
    for (const touch of touches.values()) {
        mask |= touch.mask ?? 0;
        if (touch.stick) {
            sticks[touch.stick] = [touch.x, touch.y];
        }
    }
    window.Module?._ct_web_touch_state?.(mask, ...sticks.left, ...sticks.right);
}

// Where a touch lies in an element, from -1 to 1 each way.
function offset(element, event) {
    const box = element.getBoundingClientRect();
    return [(event.clientX - box.left) / box.width * 2 - 1, (event.clientY - box.top) / box.height * 2 - 1];
}

function dpadMask([x, y]) {
    const dead = 0.33;
    return (y < -dead) << PAD_BITS.up | (y > dead) << PAD_BITS.down | (x < -dead) << PAD_BITS.left |
        (x > dead) << PAD_BITS.right;
}

function setupPad() {
    const track = (element, update) => {
        element.addEventListener('pointerdown', (event) => {
            event.preventDefault();
            try {
                element.setPointerCapture(event.pointerId);
            } catch {
                // The pointer already lifted (Android hands a quick tap's pointerdown late); its move and up still
                // reach this element.
            }
            touches.set(event.pointerId, update(event));
            sendPad();
        });
        element.addEventListener('pointermove', (event) => {
            if (touches.has(event.pointerId)) {
                touches.set(event.pointerId, update(event));
                sendPad();
            }
        });
        const release = (event) => {
            if (touches.delete(event.pointerId)) {
                update(null);
                sendPad();
            }
        };
        element.addEventListener('pointerup', release);
        element.addEventListener('pointercancel', release);
    };
    for (const button of document.querySelectorAll('#pad [data-button]')) {
        const bit = 1 << PAD_BITS[button.dataset.button];
        track(button, (event) => {
            button.classList.toggle('held', event !== null);
            return {mask: bit};
        });
    }
    const dpad = $('pad-dpad');
    track(dpad, (event) => {
        const mask = event ? dpadMask(offset(dpad, event)) : 0;
        for (const arm of dpad.children) {
            arm.classList.toggle('held', (mask & 1 << PAD_BITS[arm.dataset.dir]) !== 0);
        }
        return {mask};
    });
    for (const [id, stick] of [['pad-left', 'left'], ['pad-right', 'right']]) {
        const base = $(id);
        const knob = base.firstElementChild;
        track(base, (event) => {
            let [x, y] = event ? offset(base, event) : [0, 0];
            const length = Math.hypot(x, y);
            if (length > 1) {
                x /= length;
                y /= length;
            }
            knob.style.transform = `translate(${x * 75}%, ${y * 75}%)`;
            return {stick, x, y};
        });
    }
}

// The Capture button's frames, as the game drew them (port/web/vulkan/device.cpp, CaptureFrame), laid out in one
// image: frames the screen shows wrong but the capture shows right are the browser's, not the game's.
const kCaptureFrames = 8;
let captured = [];

function showCaptured() {
    const columns = 2;
    const width = Math.min(800, captured[0].width);
    const height = Math.round(captured[0].height * width / captured[0].width);
    const sheet = document.createElement('canvas');
    sheet.width = columns * width;
    sheet.height = Math.ceil(captured.length / columns) * height;
    const context = sheet.getContext('2d');
    const frame = document.createElement('canvas');
    captured.forEach(({pixels, width: w, height: h}, index) => {
        frame.width = w;
        frame.height = h;
        frame.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(pixels.buffer), w, h), 0, 0);
        const x = (index % columns) * width;
        const y = Math.floor(index / columns) * height;
        context.drawImage(frame, x, y, width, height);
        context.fillStyle = '#ff0';
        context.font = `${Math.round(height / 8)}px sans-serif`;
        context.fillText(String(index + 1), x + 8, y + Math.round(height / 7));
    });
    captured = [];
    sheet.toBlob((blob) => {
        const url = URL.createObjectURL(blob);
        $('captured-image').src = url;
        $('captured-save').href = url;
        $('captured').hidden = false;
        log(`capture: ${kCaptureFrames} frames ready`);
    });
}

// The GPU's name as WebGL tells it, or '' where it does not.
function gpuName() {
    try {
        const gl = document.createElement('canvas').getContext('webgl2');
        if (!gl) {
            return '';
        }
        const info = gl.getExtension('WEBGL_debug_renderer_info');
        const name = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER) ?? '');
        gl.getExtension('WEBGL_lose_context')?.loseContext();
        return name;
    } catch {
        return '';
    }
}

function hasWebGl2() {
    try {
        return !!document.createElement('canvas').getContext('webgl2');
    } catch {
        return false;
    }
}

// Which build the game runs as: WebGL2 where there is no WebGPU, and on Arm's Mali GPUs (Android phones), whose
// WebGPU showed frames before they were drawn and ran the game at half speed once made to wait (docs/WEB.md);
// WebGPU elsewhere. ?renderer=gl or ?renderer=webgpu chooses.
function chooseRenderer() {
    const asked = new URLSearchParams(location.search).get('renderer');
    if (asked === 'gl' || asked === 'webgpu') {
        return asked;
    }
    if (!navigator.gpu) {
        return 'gl';
    }
    return /mali/i.test(gpuName()) ? 'gl' : 'webgpu';
}

// How many pixels the game draws to the CSS pixel. SDL sizes the canvas by the screen's own ratio, about three on
// a phone: the frame would be several times a PC window's, on a GPU a fraction as fast. A touch screen draws at no
// more than 1.5; ?scale=1 or ?scale=3, say, chooses. SDL asks the page for devicePixelRatio, so that is what
// answers.
function pixelRatio() {
    const asked = Number(new URLSearchParams(location.search).get('scale'));
    const ratio = asked > 0 ? Math.min(asked, 4) : TOUCH_SCREEN ? Math.min(devicePixelRatio, 1.5) : devicePixelRatio;
    if (ratio !== devicePixelRatio) {
        Object.defineProperty(window, 'devicePixelRatio', {get: () => ratio, configurable: true});
    }
    return ratio;
}

// How the player chose to unpack a stored disc, 'whole' or 'play'; null when there is nothing to unpack.
let unpackChoice = null;

function start(unpack = null) {
    unpackChoice = unpack;
    $('setup').hidden = true;
    $('game').hidden = false;
    const extra = new URLSearchParams(location.search).get('args');
    const args = ['--run-dir', `/opfs/${HOME}`];
    if (server) {
        args.push('--env', `CT_WEB_SERVER_DATA=${new URL(SERVER_DATA, location.href)}`, '--data', '/server/data');
        // The server takes the page's log, and with it the game's rates.
        args.push('--log-fps');
        startReport();
    }
    if (refreshRate > 0) {
        args.push('--env', `CT_WEB_REFRESH=${refreshRate}`);
    }
    if (unpack) {
        args.push('--env', `CT_WEB_UNPACK=${unpack}`);
    }
    if (extra) {
        args.push(...extra.split(/\s+/).filter(Boolean));
    }
    window.ctLogging = true;
    kept.length = 0;
    log(`${new Date().toISOString()} ${navigator.userAgent}`);
    const renderer = chooseRenderer();
    const screenRatio = devicePixelRatio;
    log(`darkchronicle ${args.join(' ')} (${renderer === 'gl' ? 'WebGL2' : 'WebGPU'}, ${gpuName() || 'GPU unknown'}, ` +
        `${pixelRatio()} pixels to the CSS pixel of the screen's ${screenRatio}, page ${innerWidth}x${innerHeight})`);
    log(`device: ${navigator.hardwareConcurrency} processors, ${navigator.deviceMemory ?? 'unknown'} GiB of memory, ` +
        `screen ${screen.width}x${screen.height} at ${refreshRate || 'an unknown rate of'} Hz`);
    if (unpack) {
        // Beside the times the game logs, so an estimate that is off shows by how much.
        const {whole, play, speed, jobs} = estimateUnpack();
        log(`unpack: ${unpack}, expected ${Math.round(unpack === 'play' ? play : whole)} s before the game starts and ` +
            `${Math.round(whole)} s in all (${jobs} at a time, device speed ${Math.round(speed)})`);
    }
    if (server) {
        probeNetwork();
    }
    const canvas = $('canvas');
    // Threads still running when the game exits can abort as it takes them down; the exit is what matters.
    let exited = false;
    window.Module = {
        arguments: args,
        canvas,
        print,
        printErr: print,
        onRuntimeInitialized() {
            if (TOUCH_SCREEN) {
                Module._ct_web_touch_screen(1);
            }
        },
        ctTouchVisible(visible) {
            $('pad').hidden = !(visible && TOUCH_SCREEN);
            if ($('pad').hidden && touches.size > 0) {
                touches.clear();
                sendPad();
            }
        },
        dcCaptured(pixels, width, height, remaining) {
            captured.push({pixels, width, height});
            if (remaining <= 0) {
                showCaptured();
            }
        },
        onExit(code) {
            exited = true;
            log(`exit=${code}`);
            notice(`The game exited (${code}).`);
        },
        onAbort(reason) {
            log(`abort: ${reason}`);
            if (exited) {
                return;
            }
            notice('The game stopped with an error. The log has what it printed.');
            $('drawer').hidden = false;
        },
    };
    const script = document.createElement('script');
    script.src = renderer === 'gl' ? 'darkchronicle_gl.js' : 'darkchronicle.js';
    document.body.appendChild(script);
    canvas.addEventListener('click', () => canvas.focus());
    canvas.focus();
}

// The last run's log, if a run left one (keepLog), whether or not this browser can run the game.
function showPreviousLog() {
    let previous = null;
    try {
        previous = localStorage.getItem(LOG_KEY);
    } catch {
        previous = null;
    }
    if (!previous) {
        return;
    }
    $('last-log').textContent = previous;
    $('last-log-toggle').hidden = false;
    $('last-log-toggle').addEventListener('click', () => {
        $('last-log').hidden = !$('last-log').hidden;
        $('last-log').scrollTop = $('last-log').scrollHeight;
    });
}

async function main() {
    showPreviousLog();
    showGames();
    $('fullscreen').addEventListener('click', () => {
        if (document.fullscreenElement) {
            document.exitFullscreen();
        } else {
            $('game').requestFullscreen?.().catch(() => {});
        }
    });
    setupPad();
    $('capture').addEventListener('click', () => {
        if (window.Module && Module._dc_web_capture) {
            captured = [];
            log(`capture: the next ${kCaptureFrames} frames`);
            Module._dc_web_capture(kCaptureFrames);
        }
        $('canvas').focus();
    });
    $('captured-close').addEventListener('click', () => {
        $('captured').hidden = true;
        $('canvas').focus();
    });
    $('log-toggle').addEventListener('click', () => {
        $('drawer').hidden = !$('drawer').hidden;
        $('log').scrollTop = $('log').scrollHeight;
        $('canvas').focus();
    });

    const isolated = await isolate();
    if (isolated === null) {
        return;
    }
    if (!isolated) {
        setStatus('This browser cannot run the game: it needs cross-origin isolation for threads.');
        return;
    }
    if (!navigator.storage || !navigator.storage.getDirectory) {
        setStatus('This browser has no origin private file system to keep the game data in.');
        return;
    }
    if (!navigator.gpu && !hasWebGl2()) {
        setStatus('This browser has neither WebGPU nor WebGL2 to draw the game with. Use a recent Chrome or Edge.');
        return;
    }
    navigator.storage.persist?.();
    measureRefresh();
    server = new URLSearchParams(location.search).has('local') ? null : await serverData();

    const choose = (store) => async (event) => {
        const chosen = event.target.files;
        if (!chosen || chosen.length === 0) {
            return;
        }
        setStatus('Copying into this browser…');
        try {
            await store(chosen);
        } catch (error) {
            setStatus(`Could not store it: ${error.message ?? error}`);
            $('progress').hidden = true;
            return;
        }
        event.target.value = '';
        await refresh();
    };
    $('file').addEventListener('change', choose(storeFiles));
    $('folder').addEventListener('change', choose(storeFolder));
    $('start').addEventListener('click', () => start());
    $('unpack-whole').addEventListener('click', () => start('whole'));
    $('unpack-play').addEventListener('click', () => start('play'));
    $('forget').addEventListener('click', async () => {
        await removeAll();
        await refresh();
    });
    $('forget-disc').addEventListener('click', async () => {
        const home = await gameDirectory();
        for (const kind of ['disc.iso', 'disc']) {
            await home.removeEntry(kind, {recursive: true}).catch(() => {});
        }
        await refresh();
    });
    await refresh();
}

// Errors the page itself meets go to the log too, as the game's do.
window.addEventListener('error', (event) => log(`[page error] ${event.message} (${event.filename}:${event.lineno})`));
window.addEventListener('unhandledrejection', (event) => log(`[page error] ${event.reason?.stack ?? event.reason}`));

main().catch((error) => setStatus(`Startup failed: ${error}`));
