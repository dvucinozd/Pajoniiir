// Review probe: executes production JS with a controlled event schedule.
import fs from 'node:fs';
import vm from 'node:vm';
const src = fs.readFileSync('firmware/main-deck-p4/components/web_server/web/app.js', 'utf8');
let now = 1000;
const requests = [], timers = [];
const c = vm.createContext({
    console, AbortController, Date: { now: () => now },
    document: { readyState: 'loading', addEventListener() {}, getElementById() { return null; } },
    setTimeout: (fn) => { timers.push(fn); return timers.length; },
    setInterval() {}, clearTimeout() {},
    fetch: (url) => { requests.push(url); return Promise.resolve({ ok:true }); }
});
vm.runInContext(src, c);
const send = v => vm.runInContext(`throttledSend('fader', v => '/fader=' + v, ${v})`, c);
send(100); now = 1040; send(200);
/* The browser deferred a due timer. A later input event runs first. */
now = 1100; send(300); timers.shift()();
console.log('THROTTLE_SEND_ORDER', JSON.stringify(requests), '(expected final value 300)');
