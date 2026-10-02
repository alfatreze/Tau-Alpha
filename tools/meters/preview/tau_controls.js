/* Builds sliders / selects / toggles from a meter's params[]; honours `when`; preset dropdown; optional collapsible groups (meter.groups +
 * param.group); an (i) tooltip per parameter (param.help, enum param.value_help). Browser only.
 * build(host, meter, onChange) -> { get(), set(key, value), setMany(obj) }; onChange(paramsObject) fires after every change. */
(function (root) {
  let tip = null, tipFor = null, tipPinned = null;
  function tipEl() {
    if (tip) return tip;
    tip = document.createElement('div'); tip.id = 'tip'; tip.setAttribute('role', 'tooltip'); tip.hidden = true; document.body.appendChild(tip);
    document.addEventListener('click', (e) => { if (tipPinned && !e.target.closest('.info')) hideTip(true); });
    document.addEventListener('keydown', (e) => { if (e.key === 'Escape') hideTip(true); });
    return tip;
  }
  function showTip(anchor, fill) {
    const t = tipEl(); tipFor = anchor; fill(t); t.hidden = false;
    const r = anchor.getBoundingClientRect(), w = t.offsetWidth, h = t.offsetHeight;
    const x = Math.min(Math.max(8, r.left + r.width / 2 - w / 2), innerWidth - w - 8);
    let y = r.bottom + 8;
    if (y + h > innerHeight - 8) y = Math.max(8, r.top - h - 8);
    t.style.left = x + 'px'; t.style.top = y + 'px';
  }
  function hideTip(force) { if (!tip) return; if (tipPinned && !force) return; tipPinned = null; tip.hidden = true; tipFor = null; }

  function build(host, meter, onChange, opts) {
    opts = opts || {};   // opts.valueNote(key, valueIndex) -> text appended to that option's line in the tooltip; opts.noteKeys = Set of parameter keys that get it
    host.innerHTML = '';
    const cur = Object.assign({}, ...meter.params.map((x) => ({ [x.key]: x.default })));
    const rows = {}, fills = {}, inputs = {}, groupEls = [];
    const visible = (x) => Object.entries(x.when || {}).every(([k, v]) => cur[k] === v);
    const fire = () => {
      for (const x of meter.params) rows[x.key].style.display = visible(x) ? '' : 'none';
      for (const g of groupEls) g.el.style.display = g.keys.some((k) => rows[k].style.display !== 'none') ? '' : 'none';
      if (tip && !tip.hidden && tipFor && fills[tipFor.dataset.key]) fills[tipFor.dataset.key](tip);
      onChange(Object.assign({}, cur));
    };
    if (meter.presets.length) {
      const row = document.createElement('label'); row.textContent = 'Preset ';
      const sel = document.createElement('select'); sel.id = 'presetsel';
      meter.presets.forEach((pr, i) => { const o = document.createElement('option'); o.value = i; o.textContent = pr.name; sel.appendChild(o); });
      sel.value = meter.default_preset;
      sel.onchange = () => { Object.assign(cur, meter.presets[sel.value].values); sync(); fire(); };
      row.appendChild(sel); host.appendChild(row);
    }
    const holders = {};                                // group key -> element that receives that group's rows
    if (meter.groups) for (const g of meter.groups) {
      const d = document.createElement('details'); d.className = 'grp'; d.open = true;
      const s = document.createElement('summary'); s.textContent = g.label; d.appendChild(s);
      const body = document.createElement('div'); body.className = 'grpbody'; d.appendChild(body);
      host.appendChild(d); holders[g.key] = body; groupEls.push({ el: d, keys: [], key: g.key });
    }
    for (const x of meter.params) {
      const row = document.createElement('label'); row.appendChild(document.createTextNode(x.label + ' ')); rows[x.key] = row;
      let inp;
      const out = document.createElement('span'); out.className = 'val';
      if (x.type === 'enum') { inp = document.createElement('select'); x.values.forEach((v, i) => { const o = document.createElement('option'); o.value = i; o.textContent = v; inp.appendChild(o); }); }
      else if (x.type === 'bool') { inp = document.createElement('input'); inp.type = 'checkbox'; }
      else if (x.kind === 'rgb565') {                 // colour picker + hex field; the stored value is the Pocket's RGB565
        inp = document.createElement('span'); inp.className = 'rgb';
        const pick = document.createElement('input'); pick.type = 'color'; const hex = document.createElement('input'); hex.type = 'text'; hex.size = 7; hex.setAttribute('aria-label', x.label + ' hex');
        const to565 = (r, g, b) => (Math.trunc((r * 31 + 127) / 255) << 11) | (Math.trunc((g * 63 + 127) / 255) << 5) | Math.trunc((b * 31 + 127) / 255);
        const toHex = (c) => '#' + [(c >> 11) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31].map((v) => Math.round(v).toString(16).padStart(2, '0')).join('');
        const show = () => { const h = toHex(cur[x.key]); pick.value = h; hex.value = h; out.textContent = '0x' + cur[x.key].toString(16).toUpperCase().padStart(4, '0'); };
        const set = (h) => { const m = /^#?([0-9a-f]{6})$/i.exec(String(h).trim()); if (!m) return false; const v = parseInt(m[1], 16); cur[x.key] = to565(v >> 16, (v >> 8) & 255, v & 255); show(); fire(); return true; };
        pick.oninput = () => set(pick.value); hex.onchange = () => { if (!set(hex.value)) show(); };
        inp.appendChild(pick); inp.appendChild(hex); inp.sync = show; inp.rgb = true;
      }
      else { inp = document.createElement('input'); inp.type = 'range'; inp.min = x.min; inp.max = x.max; inp.step = x.step; }
      let info = null;
      if (x.help) {                                   // (i): hover, focus or tap shows the explanation; enum choices get their own lines, the selected one bold
        info = document.createElement('button'); info.type = 'button'; info.className = 'info'; info.textContent = 'i'; info.dataset.key = x.key;
        info.setAttribute('aria-label', 'About ' + x.label);
        fills[x.key] = (t) => {
          t.textContent = ''; const a = document.createElement('div'); a.className = 'tiph'; a.textContent = x.help; t.appendChild(a);
          const notes = opts.valueNote && opts.noteKeys && opts.noteKeys.has(x.key) && x.type === 'enum';
          if (x.value_help || notes) (x.value_help || x.values).forEach((s, i) => { const d = document.createElement('div'); d.textContent = s + (notes ? '  [' + opts.valueNote(x.key, i) + ']' : ''); if (i === cur[x.key]) d.className = 'sel'; t.appendChild(d); });
        };
        info.onmouseenter = info.onfocus = () => showTip(info, fills[x.key]);
        info.onmouseleave = info.onblur = () => hideTip(false);
        info.onclick = (e) => { e.preventDefault(); e.stopPropagation(); if (tipPinned === info) hideTip(true); else { showTip(info, fills[x.key]); tipPinned = info; } };
      }
      if (!inp.rgb) inp.oninput = inp.onchange = () => { cur[x.key] = x.type === 'bool' ? (inp.checked ? 1 : 0) : Number(inp.value); out.textContent = x.type === 'enum' ? x.values[cur[x.key]] : (cur[x.key] + (x.offset || 0)) + (x.unit || ''); fire(); };
      inputs[x.key] = [inp, out, x]; row.appendChild(inp); row.appendChild(out); if (info) row.appendChild(info);
      const gk = x.group && holders[x.group] ? x.group : null;
      (gk ? holders[gk] : host).appendChild(row);
      if (gk) groupEls.find((g) => g.key === gk).keys.push(x.key);
    }
    function sync() { for (const [k, [inp, out, x]] of Object.entries(inputs)) { if (inp.rgb) { inp.sync(); continue; } if (x.type === 'bool') inp.checked = !!cur[k]; else inp.value = cur[k]; out.textContent = x.type === 'enum' ? x.values[cur[k]] : (cur[k] + (x.offset || 0)) + (x.unit || ''); } }
    sync(); fire();
    return {
      get: () => Object.assign({}, cur),
      set: (k, v) => { cur[k] = v; sync(); fire(); },
      setMany: (o) => { Object.assign(cur, o); sync(); fire(); },
    };
  }
  root.TauControls = { build };
})(typeof globalThis !== 'undefined' ? globalThis : window);
