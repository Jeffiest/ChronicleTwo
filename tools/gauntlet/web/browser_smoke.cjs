// The web build's page in headless Chromium, as port/web/test/browser_smoke.mjs runs it: from a plain
// static server, waits for sw.js to isolate the page, stores the archive in the origin private file system
// under chronicletwo/ (shell.js HOME; the game runs --run-dir /opfs/chronicletwo),
// starts the game and saves what it printed. CommonJS so the global Playwright resolves without a package here.
//
//   node browser_smoke.cjs <page url> <archive url> <log file> [game arguments...]

const {writeFile} = require('node:fs/promises');
const path = require('node:path');

function loadPlaywright() {
    try {
        return require('playwright');
    } catch (error) {
        const root = process.env.NPM_GLOBAL_ROOT;
        if (root) {
            return require(path.join(root, 'playwright'));
        }
        throw error;
    }
}

async function main() {
    const [pageUrl, archiveUrl, logFile, ...gameArgs] = process.argv.slice(2);
    const {chromium} = loadPlaywright();
    const lines = [];
    const note = (text) => {
        lines.push(text);
        console.log(text);
    };
    // WebGPU on SwiftShader's Vulkan, so the renderer runs without a GPU; a presented canvas loses the device
    // there, so the game runs --offscreen. ?renderer=gl in the page url runs it on WebGL2 instead.
    const browser = await chromium.launch({
        args: ['--no-sandbox', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader',
            '--use-webgpu-adapter=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader'],
    });
    let status = 1;
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
        await page.evaluate(async (archive) => {
            const bytes = await (await fetch(archive)).arrayBuffer();
            const root = await navigator.storage.getDirectory();
            const dir = await root.getDirectoryHandle('chronicletwo', {create: true});
            const writable = await (await dir.getFileHandle('data.zip', {create: true})).createWritable();
            await writable.write(bytes);
            await writable.close();
        }, archiveUrl);
        await page.reload();
        await page.waitForSelector('#ready:not([hidden])', {timeout: 60000});
        note(`status: ${await page.textContent('#status')}`);
        await page.click('#start');
        try {
            await page.waitForFunction(() => /^(exit=|abort:)/m.test(document.getElementById('log').textContent),
                null, {timeout: 180000});
            status = 0;
        } catch {
            note('timed out waiting for the game to exit');
        }
        note('--- page log ---');
        note(await page.textContent('#log'));
    } catch (error) {
        note(`[smoke error] ${error.message}`);
    } finally {
        await browser.close();
        await writeFile(logFile, lines.join('\n') + '\n');
    }
    process.exit(status);
}

main();
