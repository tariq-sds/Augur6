// Mirror Bridge (Max for Live, Main track).
// Receives Mirror's plan over OSC (udp 9137), moves Live's real track faders and transposes clips,
// reports fader levels back to Mirror (udp 9138). Every change is recorded so Restore can undo it.
// ES5 only (Max JS).
autowatch = 0;
inlets = 1;
outlets = 2;   // 0: OSC out (udpsend), 1: UI state (jsui)

var song = null, ready = false, task = null;
var plan = [], planOpen = false;
var history = [];          // [{kind:'fader', id, old, next, name} | {kind:'clip'|'notes', ...}]
var showing = 'new';       // A/B state
var status = 'Waiting for Mirror...';
var lastResults = [];      // [{name, msg}]
var lastMirror = 0;

function api(p) { return new LiveAPI(null, p); }
function txt(v) { return Array.isArray (v) ? v.join(' ') : String(v); }
function num(o, p) { return Number(o.get(p)); }
function ids(a) { var r = []; if (!Array.isArray (a)) return r; for (var i = 0; i < a.length; i++) if (a[i] === 'id' && Number(a[i + 1]) > 0) r.push(Number(a[++i])); return r; }

function shownDb(param, v)
{
    var s = txt(param.call('str_for_value', v)).replace(/,/g, '.');
    if (/inf/i.test(s) || s.indexOf('∞') >= 0) return -Infinity;
    var m = s.match(/[-+]?\d+(?:\.\d+)?/);
    if (!m) throw Error('fader dB unavailable');
    return Number(m[0]);
}

// Fader value for a dB target, by bisection on Live's own display curve
function valueForDb(param, db)
{
    var lo = 0, hi = 1;
    for (var i = 0; i < 24; i++)
    {
        var mid = (lo + hi) / 2;
        if (shownDb(param, mid) < db) lo = mid; else hi = mid;
    }
    return (lo + hi) / 2;
}

function init()
{
    try
    {
        song = api('live_set');
        var me = api('this_device canonical_parent');
        if (Number(me.id) !== Number(api('live_set master_track').id)) status = 'Tip: put Mirror Bridge on the Main track.';
        else status = 'Ready. Waiting for Mirror...';
        ready = true;
    }
    catch (e) { status = 'Live API unavailable: ' + e; }
    if (!task) { task = new Task(tick, this); task.interval = 2000; task.repeat(); }
    draw();
}

function tracks()
{
    var out = [], list = ids(song.get('tracks'));
    for (var i = 0; i < list.length; i++)
    {
        var t = api('id ' + list[i]);
        if (!num(t, 'has_audio_output')) continue;
        out.push({ id: list[i], t: t, name: txt(t.get('name')) });
    }
    return out;
}

function findTrack(name)
{
    var all = tracks(), hit = [];
    for (var i = 0; i < all.length; i++) if (all[i].name === name) hit.push(all[i]);
    if (hit.length === 0) throw Error('no track called "' + name + '"');
    if (hit.length > 1) throw Error('more than one track called "' + name + '": rename them');
    return hit[0];
}

// every 2 s: say hello and report fader levels so Mirror can include them in its balance
function tick()
{
    if (!ready) return;
    outlet(0, '/bridge/hello', 'mirror-bridge-1.0');
    try
    {
        var all = tracks();
        for (var i = 0; i < all.length; i++)
        {
            var v = api(all[i].t.unquotedpath + ' mixer_device volume');
            var d = shownDb(v, num(v, 'value'));
            if (isFinite(d)) outlet(0, '/bridge/fader', all[i].name, d);
        }
    }
    catch (e) { post('Mirror Bridge: ' + e + '\n'); }
    draw();
}

function result(name, msg) { lastResults.push({ name: name, msg: msg }); outlet(0, '/bridge/result', name, msg); }

function withUndo(f)
{
    var opened = false;
    try { song.call('begin_undo_step'); opened = true; f(); }
    finally { if (opened) song.call('end_undo_step'); }
}

function applyGain(name, delta)
{
    var tr = findTrack(name);
    var v = api(tr.t.unquotedpath + ' mixer_device volume');
    if (num(v, 'automation_state') !== 0) throw Error('volume is automated, left alone');
    if (!num(v, 'is_enabled')) throw Error('fader is mapped or disabled');
    var old = num(v, 'value');
    var before = shownDb(v, old);
    if (!isFinite(before)) throw Error('fader at -inf, left alone');
    var targetDb = Math.max(-70, Math.min(6, before + delta));
    var next = valueForDb(v, targetDb);
    if (Math.abs(next - old) < 1e-5) { result(name, 'unchanged'); return; }
    v.set('value', next);
    history.push({ kind: 'fader', id: Number(v.id), old: old, next: next, name: name });
    var after = shownDb(v, num(v, 'value'));
    result(name, 'fader ' + before.toFixed(1) + ' -> ' + after.toFixed(1) + ' dB');
}

function clipsOf(tr)
{
    var out = [];
    var arr = [];
    try { arr = ids(tr.t.get('arrangement_clips')); } catch (e) {}
    for (var i = 0; i < arr.length; i++) out.push(api('id ' + arr[i]));
    var slots = ids(tr.t.get('clip_slots'));
    for (var j = 0; j < slots.length; j++)
    {
        var s = api('id ' + slots[j]);
        if (num(s, 'has_clip')) { var c = api(s.unquotedpath + ' clip'); if (Number(c.id) > 0) out.push(c); }
    }
    return out;
}

function shiftMidiClip(c, semis)
{
    var len = num(c, 'length');
    var raw = c.call('get_notes_extended', 0, 128, 0, len);
    var data = typeof raw === 'string' ? JSON.parse(raw) : (Array.isArray (raw) ? JSON.parse(raw.join(' ')) : raw);
    var notes = data.notes || [];
    for (var i = 0; i < notes.length; i++) notes[i].pitch = Math.max(0, Math.min(127, notes[i].pitch + semis));
    var d = new Dict();
    d.parse(JSON.stringify({ notes: notes }));
    c.call('apply_note_modifications', 'dictionary', d.name);
    return notes.length;
}

function applyTranspose(name, semis)
{
    var tr = findTrack(name);
    var clips = clipsOf(tr), audio = 0, midi = 0, skipped = 0;
    for (var i = 0; i < clips.length; i++)
    {
        var c = clips[i];
        if (num(c, 'is_audio_clip'))
        {
            var old = num(c, 'pitch_coarse');
            var next = Math.max(-48, Math.min(48, old + semis));
            c.set('pitch_coarse', next);
            history.push({ kind: 'clip', id: Number(c.id), old: old, next: next, name: name });
            audio++;
        }
        else
        {
            try { shiftMidiClip(c, semis); history.push({ kind: 'notes', id: Number(c.id), semis: semis, name: name }); midi++; }
            catch (e) { skipped++; post('Mirror Bridge: MIDI transpose failed on "' + name + '": ' + e + '\n'); }
        }
    }
    if (audio + midi === 0) throw Error(skipped ? 'could not transpose its MIDI clips' : 'no clips found');
    result(name, 'transposed ' + (semis > 0 ? '+' : '') + semis + ' st (' + audio + ' audio, ' + midi + ' MIDI clips' + (skipped ? ', ' + skipped + ' failed' : '') + ')');
}

// ---------------------------------------------------------------- OSC in (from Mirror)
function anything()
{
    var a = arrayfromargs(arguments);
    var addr = messagename;
    lastMirror = Date.now ? Date.now() : 0;
    if (addr === '/mirror/hello') { if (status.indexOf('Waiting') >= 0) status = 'Mirror connected.'; draw(); return; }
    if (addr === '/mirror/begin') { plan = []; planOpen = true; return; }
    if (addr === '/mirror/gain' && a.length >= 2) { plan.push({ kind: 'gain', name: String(a[0]), value: Number(a[1]) }); return; }
    if (addr === '/mirror/transpose' && a.length >= 2) { plan.push({ kind: 'transpose', name: String(a[0]), value: Number(a[1]) }); return; }
    if (addr === '/mirror/apply') { applyPlan(); return; }
    if (addr === '/mirror/restore') { restore(); return; }
}

function applyPlan()
{
    if (!ready) { status = 'Not ready'; draw(); return; }
    if (num(song, 'record_mode') || num(song, 'session_record')) { status = 'Stop recording first: nothing changed.'; outlet(0, '/bridge/status', status); draw(); return; }
    lastResults = [];
    var ok = 0, failed = 0;
    if (plan.length && plan[0].kind === 'gain') history = [];   // a new mix match replaces the previous restore point
    withUndo(function ()
    {
        for (var i = 0; i < plan.length; i++)
        {
            var p = plan[i];
            try
            {
                if (p.kind === 'gain') applyGain(p.name, p.value);
                else applyTranspose(p.name, p.value);
                ok++;
            }
            catch (e) { failed++; result(p.name, 'not changed: ' + String(e).replace(/^Error: /, '')); }
        }
    });
    plan = []; planOpen = false; showing = 'new';
    status = ok + ' change' + (ok === 1 ? '' : 's') + ' made' + (failed ? ', ' + failed + ' skipped' : '') + '. Restore undoes them (or Cmd+Z).';
    outlet(0, '/bridge/status', status);
    draw();
}

function setAll(useOld)
{
    withUndo(function ()
    {
        for (var i = history.length - 1; i >= 0; i--)
        {
            var h = history[i];
            try
            {
                var o = api('id ' + h.id);
                if (h.kind === 'fader') o.set('value', useOld ? h.old : h.next);
                else if (h.kind === 'clip') o.set('pitch_coarse', useOld ? h.old : h.next);
                else if (h.kind === 'notes') shiftMidiClip(o, useOld ? -h.semis : h.semis);
            }
            catch (e) { post('Mirror Bridge restore: ' + e + '\n'); }
        }
    });
}

function restore()
{
    if (!history.length) { status = 'Nothing to restore.'; draw(); return; }
    if (showing === 'new') setAll(true);
    history = [];
    showing = 'new';
    status = 'Restored the original levels and pitches.';
    outlet(0, '/bridge/status', status);
    draw();
}

function compare()
{
    if (!history.length) return;
    // only faders and audio clips are A/B'd; MIDI notes would be rewritten each time
    var old = showing === 'new';
    withUndo(function ()
    {
        for (var i = 0; i < history.length; i++)
        {
            var h = history[i];
            if (h.kind === 'notes') continue;
            try { var o = api('id ' + h.id); o.set(h.kind === 'fader' ? 'value' : 'pitch_coarse', old ? h.old : h.next); } catch (e) {}
        }
    });
    showing = old ? 'old' : 'new';
    status = old ? 'Hearing ORIGINAL. Click again for the matched mix.' : 'Hearing MATCHED mix.';
    draw();
}

function draw()
{
    outlet(1, 'state', JSON.stringify({ status: status, results: lastResults.slice(-6), canRestore: history.length > 0, showing: showing }));
}

// test hook (node)
if (typeof module !== 'undefined') module.exports = { init: init, tick: tick, anything: anything, restore: restore, compare: compare, valueForDb: valueForDb, _state: function () { return { history: history, status: status, results: lastResults }; } };
