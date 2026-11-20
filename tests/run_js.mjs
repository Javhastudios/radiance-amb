// Ejecuta el prototipo ORIGINAL (web/radiance-amb.html) en Node, con una implementación de
// Web Audio (node-web-audio-api) y un DOM simulado, y graba lo que suena en cada escenario.
// compare_cpp hace lo mismo con el motor en C++ y compare.mjs compara ambos.
//
//   npm install node-web-audio-api@2.2.0   (una vez, dentro de tests/)
//   node tests/run_js.mjs <carpeta-salida>
//
// Las acciones se hacen como un usuario: mover sliders (evento input), elegir en los menús
// (change), pulsar botones, pulsar teclas del ordenador y mandar MIDI por un dispositivo
// simulado. Los temporizadores de la página (setInterval del planificador de granos,
// setTimeout) se emulan sobre el reloj del AudioContext, parando el render en cada quantum.
// Todo lo que pasa (ticks, borrado de voces, cambios de material) se apunta en un registro
// que compare_cpp reproduce en el mismo orden.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { OfflineAudioContext } = require('node-web-audio-api');

const here = path.dirname(fileURLToPath(import.meta.url));
const outDir = process.argv[2] || path.join(here, 'out');
fs.mkdirSync(outDir, { recursive: true });
const SR = 48000, Q = 128;

// ── escenarios (en quantums de 128 muestras; el seno de fábrica se carga solo en q 8) ──
const allScenarios = {
  drone_default: `
q 12 click playBtn
q 700 click playBtn
q 1000 end`,
  drone_drift_fx: `
q 0 select posMode drift
q 0 select grainShape perc
q 0 slider shimmerAmount 60
q 0 slider dryWet 70
q 0 slider reverbPreDelay 40
q 0 slider reverbSpace 20
q 12 click playBtn
q 200 select filterType highpass
q 210 slider filterCutoff 40
q 260 slider filterQ 6
q 300 slider grainSize 300
q 330 slider grainDensity 40
q 380 select grainShape rev
q 420 slider shimmerTone -7
q 450 slider reverbSpace 5
q 470 slider reverbPreDelay 90
q 500 slider dryWet 20
q 560 slider masterVol 40
q 650 click playBtn
q 1000 end`,
  keys_adsr_presets: `
q 0 slider noteAttack 100
q 0 slider noteDecay 200
q 0 slider noteSustain 50
q 0 slider noteRelease 400
q 0 slider pitchSpray 30
q 20 key a
q 60 key d
q 120 key g
q 200 keyup a
q 230 preset 4
q 260 keyup d
q 300 key a
q 330 keyup g
q 360 slider pitchTranspose 7
q 400 keyup a
q 420 key a
q 440 keyup a
q 480 slider noteRelease 0
q 500 key k
q 560 keyup k
q 900 end`,
  midi_controls: `
q 0 click midiConnectBtn
q 0 select posRateMode sync
q 0 select posSyncDiv 1/8T
q 0 slider posTempo 96
q 0 slider posDepth 60
q 0 slider pitchBendRange 7
q 0 slider noteAttack 30
q 0 slider noteRelease 600
q 20 midi 144 60 100
q 40 midi 144 67 50
q 80 midi 224 0 100
q 120 midi 176 1 127
q 160 midi 176 64 127
q 180 midi 128 60 0
q 200 midi 144 72 127
q 220 midi 128 67 0
q 240 midi 128 72 0
q 280 preset 5
q 300 midi 224 0 64
q 330 midi 176 64 0
q 360 midi 144 64 90
q 420 midi 176 1 0
q 460 midi 128 64 0
q 800 end`,
  presets_gain: `
q 0 slider grainSize 400
q 0 slider grainDensity 60
q 0 slider grainAttack 0
q 0 slider grainRelease 0
q 0 slider grainJitter 60
q 0 slider stereoSpread 100
q 0 slider moveDepth 80
q 0 slider moveRate 0.9
q 0 select posMode static
q 0 slider posKnob 0.8
q 12 click playBtn
q 100 preset 1
q 200 slider sampleGain -6
q 260 preset 2
q 330 preset 3
q 360 slider sampleGain 4.5
q 420 preset 1
q 450 select filterType bandpass
q 470 slider filterSpray 90
q 500 slider pitchTranspose -12
q 520 slider pitchFine 35
q 560 click playBtn
q 900 end`,
};
const only = process.env.ONLY;
const scenarios = only ? Object.fromEntries(Object.entries(allScenarios).filter(([k]) => only.split(',').includes(k))) : allScenarios;

// ── carga del script original (con 3 marcas para el registro) ──
const html = fs.readFileSync(path.join(here, '..', 'web', 'radiance-amb.html'), 'utf8');
const first = html.indexOf('<script>');
let scriptSrc = html.slice(first + 8, html.indexOf('</script>', first));
const patch = (a, b) => { if (!scriptSrc.includes(a)) throw new Error('no encuentro en el original: ' + a); scriptSrc = scriptSrc.replace(a, b); };
patch('schedulerTimer = setInterval(schedulerTick, SCHED_INTERVAL_MS);', "schedulerTimer = setInterval(()=>{ __log('tick'); schedulerTick(); }, SCHED_INTERVAL_MS);");
patch('voice.deleteTimer = setTimeout(()=>{ voices.delete(noteId); }, removeInMs);', "voice.deleteTimer = setTimeout(()=>{ __log('del', noteId.slice(1)); voices.delete(noteId); }, removeInMs);");
patch('function finishSelectPreset(preset){', "function finishSelectPreset(preset){ __log('source', FACTORY_DEFS.findIndex(d=>d.id===preset.id), isPlaying ? 1 : 0);");

// valores iniciales de los <input type=range>
const defaults = {};
for (const m of html.matchAll(/<input type="range" id="([^"]+)"[^>]*value="([^"]+)"/g)) defaults[m[1]] = m[2];

function lcg(seed) { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }

async function runScenario(name, text) {
  const lines = text.trim().split('\n').map((l) => l.trim().split(/\s+/));
  const endQ = +lines.find((l) => l[2] === 'end')[1];
  const LEN = endQ * Q;
  const log = [];
  let curQ = 0;
  const __log = (...a) => log.push(`q ${curQ} ${a.join(' ')}`);

  // DOM simulado
  const els = new Map(), winListeners = {}, docListeners = {};
  const classList = () => { const s = new Set(); return { add: (c) => s.add(c), remove: (c) => s.delete(c), toggle: (c, on) => { const v = on === undefined ? !s.has(c) : !!on; v ? s.add(c) : s.delete(c); return v; }, contains: (c) => s.has(c) }; };
  const noop = () => {};
  const ctx2d = new Proxy({}, { get: (t, k) => (k in t ? t[k] : noop), set: (t, k, v) => { t[k] = v; return true; } });
  const makeEl = (id, tag = 'div') => {
    const el = { id, tagName: tag.toUpperCase(), listeners: {}, classList: classList(), style: {}, dataset: {}, textContent: '', value: defaults[id] ?? '', disabled: false, children: [], title: '', className: '',
      addEventListener(t, f) { (this.listeners[t] ||= []).push(f); }, removeEventListener() {},
      appendChild(c) { this.children.push(c); return c; }, removeChild() {}, click() {}, blur() {}, focus() {}, remove() {},
      getContext: () => ctx2d, getBoundingClientRect: () => ({ left: 0, top: 0, width: 600, height: 160 }), setAttribute() {}, querySelectorAll: () => [] };
    Object.defineProperty(el, 'innerHTML', { get: () => '', set: () => { el.children = []; } });
    return el;
  };
  const document = {
    getElementById: (id) => { if (!els.has(id)) els.set(id, makeEl(id)); return els.get(id); },
    createElement: (tag) => makeEl('', tag),
    querySelectorAll: () => [],
    addEventListener: (t, f) => (docListeners[t] ||= []).push(f),
    activeElement: null, visibilityState: 'visible',
    body: { appendChild() {}, removeChild() {} },
  };

  // temporizadores sobre el reloj del AudioContext
  const holder = {};
  let timerSeq = 0;
  const timers = new Map();
  const nowT = () => (holder.ctx ? holder.ctx.currentTime : 0);
  const addTimer = (fn, ms, repeat) => { const id = ++timerSeq; timers.set(id, { id, fn, period: (ms || 0) / 1000, due: nowT() + (ms || 0) / 1000, repeat }); return id; };
  const runDueTimers = async () => {
    for (;;) {
      const t = nowT();
      let best = null;
      for (const tm of timers.values()) if (tm.due <= t + 1e-9 && (!best || tm.due < best.due || (tm.due === best.due && tm.id < best.id))) best = tm;
      if (!best) return;
      if (best.repeat) best.due += best.period; else timers.delete(best.id);
      await best.fn();
    }
  };

  // La página comprueba audioCtx.state y llama a resume() si está 'suspended' (en un tick
  // incluso se salta la planificación). Aquí el render se para a propósito en cada quantum,
  // así que para la página el contexto está siempre en marcha.
  class TestContext extends OfflineAudioContext {
    constructor() { super(2, LEN, SR); holder.ctx = this; }
    get state() { return 'running'; }
    resume() { return Promise.resolve(); }
  }
  const realResume = (c) => OfflineAudioContext.prototype.resume.call(c);
  const fakeInput = { id: 'in1', name: 'Teclado de prueba', onmidimessage: null };
  const M = Object.create(Math); M.random = lcg(1);
  const context = {
    document, Math: M, console, Float32Array, Uint8Array, Int16Array, Array, Object, Map, Set, Promise, Error, String, Number, JSON, Date, parseFloat, parseInt, isFinite, DataView, ArrayBuffer,
    Blob: class {}, URL: { createObjectURL() {}, revokeObjectURL() {} }, btoa, atob, __log,
    window: { addEventListener: (t, f) => (winListeners[t] ||= []).push(f), AudioContext: TestContext,
      storage: { list: async () => ({ keys: [] }), get: async () => null, set: async () => ({}), delete: async () => ({}) } },
    navigator: { requestMIDIAccess: async () => ({ inputs: new Map([['in1', fakeInput]]), onstatechange: null }) },
    setInterval: (f, ms) => addTimer(f, ms, true), clearInterval: (id) => timers.delete(id),
    setTimeout: (f, ms) => addTimer(f, ms, false), clearTimeout: (id) => timers.delete(id),
    requestAnimationFrame: () => 0, performance: { now: () => nowT() * 1000 }, alert: (m) => console.log('  alert:', m),
    OfflineAudioContext,
  };
  vm.createContext(context);
  vm.runInContext(scriptSrc, context);

  const fire = async (list, ev) => { for (const f of list || []) await f(ev); };
  const act = async (l) => {
    const [, , cmd, a, ...rest] = l;
    log.push(l.join(' '));
    if (cmd === 'slider' || cmd === 'select') {
      const el = document.getElementById(a); el.value = rest[0];
      await fire(el.listeners[cmd === 'slider' ? 'input' : 'change'], { target: el, currentTarget: el });
    } else if (cmd === 'click') { const el = document.getElementById(a); await fire(el.listeners.click, { target: el, currentTarget: el }); }
    else if (cmd === 'key' || cmd === 'keyup') await fire(winListeners[cmd === 'key' ? 'keydown' : 'keyup'], { key: a, repeat: false, preventDefault() {} });
    else if (cmd === 'midi') fakeInput.onmidimessage({ data: Uint8Array.from([+a, ...rest.map(Number)]) });
    else if (cmd === 'preset') { const row = document.getElementById('libList').children[+a]; await fire(row.listeners.click, {}); }
    else throw new Error('acción desconocida ' + cmd);
  };

  const ctx = holder.ctx;
  const byQ = new Map();
  for (const l of lines) { if (l[2] === 'end') continue; const q = +l[1]; (byQ.get(q) || byQ.set(q, []).get(q)).push(l); }
  await new Promise((r) => setTimeout(r, 0));          // deja terminar init() (almacenamiento simulado)
  for (const l of byQ.get(0) || []) await act(l);
  await runDueTimers();
  for (let q = 1; q < endQ; ++q) {
    ctx.suspend((q * Q - 0.5) / SR).then(async () => {
      curQ = Math.round(ctx.currentTime * SR / Q);
      if (curQ !== q) console.log(`  aviso: ${name} q=${q} llegó en ${curQ}`);
      await runDueTimers();
      for (const l of byQ.get(q) || []) await act(l);
      realResume(ctx);
    });
  }
  // node-web-audio-api registra las suspensiones de forma asíncrona
  await new Promise((r) => setTimeout(r, 1000));
  const buf = await ctx.startRendering();
  const out = new Float32Array(LEN * 2);
  out.set(buf.getChannelData(0), 0); out.set(buf.getChannelData(1), LEN);
  fs.writeFileSync(path.join(outDir, name + '.js.f32'), Buffer.from(out.buffer));
  fs.writeFileSync(path.join(outDir, name + '.txt'), log.join('\n') + `\nq ${endQ} end\n`);
}

for (const [name, text] of Object.entries(scenarios)) { process.stdout.write(name + ' '); const t0 = Date.now(); await runScenario(name, text); console.log('ok', ((Date.now() - t0) / 1000).toFixed(1) + 's'); }
fs.writeFileSync(path.join(outDir, 'scenarios.json'), JSON.stringify(Object.keys(scenarios)));
console.log('escenarios JS:', Object.keys(scenarios).length, '→', outDir);
