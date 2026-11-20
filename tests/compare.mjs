// Compara la salida del prototipo original (JS) con la del motor en C++.
//   node tests/compare.mjs <carpeta>
import fs from 'node:fs';
import path from 'node:path';

const dir = process.argv[2] || 'tests/out';
const names = JSON.parse(fs.readFileSync(path.join(dir, 'scenarios.json'), 'utf8'));
const MIN_SNR = Number(process.env.MIN_SNR || 40);   // dB
let fail = 0;
for (const n of names) {
  const js = new Float32Array(fs.readFileSync(path.join(dir, n + '.js.f32')).buffer.slice(0));
  const cp = new Float32Array(fs.readFileSync(path.join(dir, n + '.cpp.f32')).buffer.slice(0));
  if (js.length !== cp.length) { console.log(`✗ ${n}: longitud distinta ${js.length} vs ${cp.length}`); fail++; continue; }
  let e = 0, d = 0, ec = 0, peak = 0, maxDiff = 0, at = 0;
  for (let i = 0; i < js.length; i++) {
    e += js[i] * js[i]; ec += cp[i] * cp[i];
    const x = js[i] - cp[i]; d += x * x;
    if (Math.abs(x) > maxDiff) { maxDiff = Math.abs(x); at = i; }
    peak = Math.max(peak, Math.abs(js[i]));
  }
  const snr = 10 * Math.log10(e / Math.max(d, 1e-30));
  const level = 10 * Math.log10(ec / Math.max(e, 1e-30));
  const ok = snr >= MIN_SNR && e > 1e-6;
  if (!ok) fail++;
  const half = js.length / 2;
  console.log(`${ok ? '✓' : '✗'} ${n.padEnd(17)} SNR ${snr.toFixed(1).padStart(6)} dB  nivel ${level >= 0 ? '+' : ''}${level.toFixed(3)} dB  pico ${peak.toFixed(3)}  dif.máx ${maxDiff.toExponential(1)} (${at < half ? 'L' : 'R'} ${(at % half / 48000).toFixed(3)} s)`);
}
console.log(fail ? `${fail} escenario(s) por debajo de ${MIN_SNR} dB` : `todos los escenarios coinciden (SNR ≥ ${MIN_SNR} dB)`);
process.exit(fail ? 1 : 0);
