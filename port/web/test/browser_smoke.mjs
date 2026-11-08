// The browser smoke run (tools/web/smoke.sh): the page from a plain static server, as GitHub Pages serves it, in
// headless Chromium. It waits for sw.js to isolate the page, stores the archive and the data.json given as URLs in
// the origin private file system as the page would (an empty archive url stores nothing: the game then reports
// the missing data and exits), starts the game and saves what it printed.
//
//   node browser_smoke.mjs <page url> <archive url or ''> <data.json url or ''> <log file> [game arguments...]

import {execSync} from 'node:child_process';
import {writeFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import path from 'node:path';

// Playwright as Node's require finds it (NODE_PATH, a global install), which an import from this file would not;
// then from NPM_GLOBAL_ROOT or `npm root -g`, as tools/gauntlet's driver does.
function loadPlaywright() {
    const require = createRequire(import.meta.url);
    try {
        return require('playwright');
    } catch (error) {
        const root = process.env.NPM_GLOBAL_ROOT || execSync('npm root -g', {encoding: 'utf8'}).trim();
        return require(path.join(root, 'playwright'));
    }
}

const {chromium} = loadPlaywright();

const [pageUrl, archiveUrl, markerUrl, logFile, ...gameArgs] = process.argv.slice(2);
const lines = [];
const note = (text) => {
    lines.push(text);
    console.log(text);
};

// WebGPU on SwiftShader's Vulkan, so the renderer runs without a GPU. Here a presented canvas loses the
// device, so the game runs --offscreen. A page url with ?renderer=gl runs it on WebGL2, on SwiftShader too.
const browser = await chromium.launch({
    args: ['--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader',
        '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader'],
});
try {
    const page = await browser.newPage();
    page.on('console', (message) => note(`[${message.type()}] ${message.text()}`));
    page.on('pageerror', (error) => note(`[pageerror] ${error.message}`));
    const url = new URL(pageUrl);
    url.searchParams.set('args', gameArgs.join(' '));

    await page.goto(url.href);
    await page.waitForFunction(() => self.crossOriginIsolated && !document.getElementById('pick').hidden
        || !document.getElementById('ready').hidden, null, {timeout: 60000});
    note(`isolated: ${await page.evaluate(() => self.crossOriginIsolated)}`);

    if (archiveUrl) {
        await page.evaluate(async ([archive, marker]) => {
            const root = await navigator.storage.getDirectory();
            const dir = await root.getDirectoryHandle('chronicletwo', {create: true});
            for (const [name, url] of [['data.zip', archive], ['data.json', marker]]) {
                if (!url) {
                    continue;
                }
                const bytes = await (await fetch(url)).arrayBuffer();
                const writable = await (await dir.getFileHandle(name, {create: true})).createWritable();
                await writable.write(bytes);
                await writable.close();
            }
        }, [archiveUrl, markerUrl]);
        await page.reload();
        await page.waitForSelector('#ready:not([hidden])', {timeout: 60000});
        note(`status: ${await page.textContent('#status')}`);
        await page.click('#start');
    } else {
        // Nothing stored: start the game all the same, as the page's Start would.
        note(`status: ${await page.textContent('#status')}`);
        await page.evaluate(() => start());
    }
    try {
        await page.waitForFunction(() => /^(exit=|abort:)/m.test(document.getElementById('log').textContent),
            null, {timeout: 180000});
    } catch {
        note('timed out waiting for the game to exit');
    }
    note('--- page log ---');
    note(await page.textContent('#log'));
} finally {
    await browser.close();
    await writeFile(logFile, lines.join('\n') + '\n');
}
