// A host/join handshake against a running co-op relay (DC_Web coop/relay protocol v1): the host opens a room,
// a second player joins it by code, each sees the other, and a binary message crosses. Exits 0 when all
// of that happened within the timeout. Runs from the relay copy in .cache, where the ws package is.
//
//   node handshake.mjs ws://127.0.0.1:<port> [game id] [protocol]

import WebSocket from 'ws';

const [url, game = 'chronicle-two', proto = '1'] = process.argv.slice(2);
const deadline = setTimeout(() => {
    console.log('FAIL handshake timed out');
    process.exit(1);
}, 20000);

function player(target) {
    return new Promise((resolve, reject) => {
        const socket = new WebSocket(target);
        const p = {socket, events: [], messages: []};
        socket.binaryType = 'nodebuffer';
        socket.on('message', (data, binary) => {
            if (binary) {
                p.messages.push({from: data[0], text: data.subarray(1).toString()});
            } else {
                const event = JSON.parse(data.toString());
                p.events.push(event);
                if (event.t === 'welcome') {
                    resolve(p);
                }
                if (event.t === 'error') {
                    reject(new Error(event.reason));
                }
            }
        });
        socket.on('error', reject);
        socket.on('close', (code) => reject(new Error(`closed ${code}`)));
        p.send = (to, text) => socket.send(Buffer.concat([Buffer.from([to]), Buffer.from(text)]));
    });
}

const wait = (predicate) => new Promise((resolve) => {
    const timer = setInterval(() => {
        if (predicate()) {
            clearInterval(timer);
            resolve();
        }
    }, 20);
});

let failures = 0;
const check = (ok, what) => {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
    if (!ok) {
        failures++;
    }
};

try {
    const host = await player(`${url}/v1/host?game=${game}&proto=${proto}&name=Host`);
    const welcome = host.events.find((e) => e.t === 'welcome');
    check(welcome && welcome.id === 0 && /^[A-Z0-9]{5}$/.test(welcome.code), `host welcomed with a room code (${welcome && welcome.code})`);
    const guest = await player(`${url}/v1/join/${welcome.code}?game=${game}&proto=${proto}&name=Guest`);
    const guestWelcome = guest.events.find((e) => e.t === 'welcome');
    check(guestWelcome && guestWelcome.id === 1 && guestWelcome.peers.some((p) => p.id === 0), 'guest welcomed and sees the host');
    await wait(() => host.events.some((e) => e.t === 'join' && e.id === 1));
    check(true, 'host saw the guest join');
    host.send(1, 'hello from the host');
    guest.send(0, 'hello from the guest');
    await wait(() => host.messages.length > 0 && guest.messages.length > 0);
    check(guest.messages[0].from === 0 && guest.messages[0].text === 'hello from the host', 'guest received the host message');
    check(host.messages[0].from === 1 && host.messages[0].text === 'hello from the guest', 'host received the guest message');
    host.socket.close();
    guest.socket.close();
} catch (error) {
    check(false, `handshake: ${error.message}`);
}
clearTimeout(deadline);
console.log(failures ? `FAIL ${failures} check(s)` : 'PASS handshake');
process.exit(failures ? 1 : 0);
