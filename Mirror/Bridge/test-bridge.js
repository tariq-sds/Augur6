// Offline test of mirror-bridge.js against a simulated Live API (node test-bridge.js)
'use strict';
const fs = require('fs'), vm = require('vm'), path = require('path');

// ---- a tiny Live model
function faderDb(v) { if (v <= 0) return -Infinity; return v >= 0.85 ? (v - 0.85) / 0.15 * 6 : 40 * Math.log10(v / 0.85) * (v < 0.4 ? 1.6 : 1); }
const objects = {};
let nextId = 1;
function add(o) { o.id = nextId++; objects[o.id] = o; return o; }
function makeTrack(name, db, clips) {
  const vol = add({ type: 'param', value: 0, automation_state: 0, is_enabled: 1 });
  // set the value whose shown dB is closest to the wanted one
  let lo = 0, hi = 1; for (let i = 0; i < 40; i++) { const m = (lo + hi) / 2; if (faderDb(m) < db) lo = m; else hi = m; } vol.value = (lo + hi) / 2;
  const clipObjs = clips.map(c => add(Object.assign({ type: 'clip', length: 16 }, c)));
  const slots = clipObjs.map(c => add({ type: 'slot', has_clip: 1, clip: c }));
  const t = add({ type: 'track', name, has_audio_output: 1, vol, slots, arrangement: [] });
  return t;
}
const master = add({ type: 'track', name: 'Main', has_audio_output: 1 });
const tracks = [
  makeTrack('Beat', 0, [{ is_audio_clip: 1, pitch_coarse: 0 }]),
  makeTrack('Low End', -3, [{ is_audio_clip: 0, notes: [{ pitch: 45, start_time: 0, duration: 1 }, { pitch: 41, start_time: 4, duration: 1 }] }]),
  makeTrack('Pad', -6, [{ is_audio_clip: 1, pitch_coarse: 0 }]),
  makeTrack('Stab', -2, [{ is_audio_clip: 1, pitch_coarse: 2 }, { is_audio_clip: 0, notes: [{ pitch: 70, start_time: 0, duration: 0.5 }] }]),
  makeTrack('Automated', 0, []),
];
tracks[4].vol.automation_state = 1;
const song = add({ type: 'song', record_mode: 0, session_record: 0, undo: 0 });
const sent = [], posts = [];

function resolve(p) {
  p = String(p);
  if (p.startsWith('id ')) return objects[Number(p.slice(3))] || { id: 0 };
  if (p === 'live_set') return song;
  if (p === 'live_set master_track') return master;
  if (p === 'this_device canonical_parent') return master;
  const m = p.match(/^id (\d+) mixer_device volume$/) || p.match(/^path(\d+) mixer_device volume$/);
  if (m) return objects[Number(m[1])].vol;
  const c = p.match(/^path(\d+) clip$/);
  if (c) return objects[Number(c[1])].clip;
  throw Error('unknown path ' + p);
}
function LiveAPI(cb, p) {
  const o = resolve(p);
  this.o = o; this.id = o.id; this.unquotedpath = 'path' + o.id;
}
LiveAPI.prototype.get = function (k) {
  const o = this.o;
  if (o.type === 'song' && k === 'tracks') return [].concat(...tracks.map(t => ['id', t.id]));
  if (o.type === 'track' && k === 'clip_slots') return [].concat(...o.slots.map(s => ['id', s.id]));
  if (o.type === 'track' && k === 'arrangement_clips') return [];
  return [o[k]];
};
LiveAPI.prototype.set = function (k, v) { if (song.undo !== 1) throw Error('write outside undo step'); this.o[k] = v; };
LiveAPI.prototype.call = function (fn, ...a) {
  const o = this.o;
  if (fn === 'str_for_value') { const d = faderDb(Number(a[0])); return isFinite(d) ? d.toFixed(1) + ' dB' : '-inf dB'; }
  if (fn === 'begin_undo_step') { song.undo = 1; return; }
  if (fn === 'end_undo_step') { song.undo = 0; return; }
  if (fn === 'get_notes_extended') return JSON.stringify({ notes: o.notes.map(n => Object.assign({}, n)) });
  if (fn === 'apply_note_modifications') { const d = dicts[a[1]]; o.notes = d.notes; return; }
  throw Error('unknown call ' + fn);
};
const dicts = {}; let dictN = 0;
function Dict() { this.name = 'd' + (++dictN); }
Dict.prototype.parse = function (s) { dicts[this.name] = JSON.parse(s); };
function Task(f) { this.f = f; } Task.prototype.repeat = function () {};

const ctx = { LiveAPI, Dict, Task, post: s => posts.push(s), outlet: (n, ...a) => { if (n === 0) sent.push(a); }, arrayfromargs: a => Array.prototype.slice.call(a), messagename: '', module: { exports: {} }, JSON, Math, Date, String, Number, Error, isFinite, Infinity };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(__dirname, 'mirror-bridge.js'), 'utf8'), ctx);
const B = ctx.module.exports;
function osc(addr, ...args) { ctx.messagename = addr; ctx.anything(...args); }

let failures = 0;
function check(ok, what) { console.log((what + ' ').padEnd(64, '.') + (ok ? ' ok' : ' FAIL')); if (!ok) failures++; }
const dbOf = t => faderDb(t.vol.value);

B.init();
B.tick();
check(sent.some(m => m[0] === '/bridge/hello'), 'sends hello');
const reported = sent.filter(m => m[0] === '/bridge/fader');
check(reported.length === 5 && Math.abs(reported.find(m => m[1] === 'Pad')[2] + 6) < 0.15, 'reports fader levels by track name');

const before = tracks.map(dbOf);
osc('/mirror/begin', 1);
osc('/mirror/gain', 'Beat', -0.43);
osc('/mirror/gain', 'Low End', 2.9);
osc('/mirror/gain', 'Pad', -4.5);
osc('/mirror/gain', 'Stab', 0.77);
osc('/mirror/gain', 'Automated', 3);
osc('/mirror/gain', 'Nope', 1);
osc('/mirror/apply', 1);
const after = tracks.map(dbOf);
console.log('  faders before', before.map(v => v.toFixed(2)).join(' '), '\n  faders after ', after.map(v => v.toFixed(2)).join(' '));
check(Math.abs(after[0] - before[0] + 0.43) < 0.1 && Math.abs(after[1] - before[1] - 2.9) < 0.1 && Math.abs(after[2] - before[2] + 4.5) < 0.1, 'moves faders by the requested dB');
check(after[4] === before[4], 'leaves automated volume alone');
check(B._state().results.some(r => r.name === 'Nope' && /no track/.test(r.msg)), 'reports a missing track instead of guessing');
check(song.undo === 0, 'closes the undo step');

B.compare();
check(Math.abs(dbOf(tracks[2]) - before[2]) < 0.05, 'Hear original puts the old levels back');
B.compare();
check(Math.abs(dbOf(tracks[2]) - after[2]) < 0.05, 'Hear matched returns to the new levels');

osc('/mirror/transpose', 'Stab', -1);
osc('/mirror/apply', 2);
check(objects[tracks[3].slots[0].clip.id].pitch_coarse === 1, 'audio clip transposed -1 st (clip Transpose)');
check(tracks[3].slots[1].clip.notes[0].pitch === 69, 'MIDI clip notes moved -1 st');

B.restore();
const restored = tracks.map(dbOf);
check(restored.every((v, i) => Math.abs(v - before[i]) < 0.05), 'Restore puts every fader back');
check(tracks[3].slots[0].clip.pitch_coarse === 2 && tracks[3].slots[1].clip.notes[0].pitch === 70, 'Restore undoes the transposes');

song.record_mode = 1;
osc('/mirror/begin', 3); osc('/mirror/gain', 'Pad', 3); osc('/mirror/apply', 1);
check(Math.abs(dbOf(tracks[2]) - before[2]) < 0.05, 'does nothing while recording');
console.log('posts', posts.slice(0,5)); console.log('results', JSON.stringify(B._state().results)); console.log('failures:', failures);
process.exit(failures ? 1 : 0);
