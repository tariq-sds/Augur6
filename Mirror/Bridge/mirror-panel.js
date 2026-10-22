// Mirror Bridge panel (jsui)
autowatch = 0;
inlets = 1;
outlets = 1;
mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var data = { status: 'Waiting for Mirror...', results: [], canRestore: false, showing: 'new' }, hits = [];

function state(s) { data = JSON.parse(s); mgraphics.redraw(); }
function rect(x, y, w, h, c) { mgraphics.set_source_rgba(c); mgraphics.rectangle(x, y, w, h); mgraphics.fill(); }
function label(s, x, y, size, c) { mgraphics.set_source_rgba(c || [0.91, 0.93, 0.95, 1]); mgraphics.select_font_face('Arial Bold'); mgraphics.set_font_size(size || 11); mgraphics.move_to(x, y); mgraphics.show_text(String(s)); }
function button(s, x, y, w, cmd, on, enabled)
{
    rect(x, y, w, 24, enabled === false ? [0.1, 0.12, 0.14, 1] : on ? [1, 0.75, 0.3, 1] : [0.16, 0.19, 0.24, 1]);
    label(s, x + 10, y + 16, 11, enabled === false ? [0.4, 0.43, 0.48, 1] : on ? [0.06, 0.06, 0.08, 1] : [0.95, 0.97, 1, 1]);
    if (enabled !== false) hits.push({ x: x, y: y, w: w, h: 24, cmd: cmd });
}

function paint()
{
    hits = [];
    rect(0, 0, 420, 169, [0.055, 0.063, 0.08, 1]);
    label('MIRROR BRIDGE', 10, 22, 15);
    label('MOVES LIVE FADERS FOR MIRROR', 10, 37, 8.5, [0.53, 0.57, 0.63, 1]);
    button(data.showing === 'old' ? 'Hear matched' : 'Hear original', 200, 8, 110, 'compare', data.showing === 'old', data.canRestore);
    button('Restore', 318, 8, 92, 'restore', false, data.canRestore);
    label(String(data.status || '').slice(0, 70), 10, 58, 10.5, [0.42, 0.88, 0.63, 1]);
    var r = data.results || [];
    for (var i = 0; i < r.length; i++)
    {
        var y = 66 + i * 16;
        rect(8, y, 404, 15, i % 2 ? [0.07, 0.08, 0.1, 1] : [0.09, 0.1, 0.13, 1]);
        label(String(r[i].name).slice(0, 22), 12, y + 11, 10);
        label(String(r[i].msg).slice(0, 44), 150, y + 11, 10, /not changed/.test(r[i].msg) ? [1, 0.48, 0.35, 1] : [0.75, 0.8, 0.86, 1]);
    }
}

function onclick(x, y)
{
    for (var i = 0; i < hits.length; i++)
    {
        var h = hits[i];
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) { outlet(0, h.cmd); return; }
    }
}
