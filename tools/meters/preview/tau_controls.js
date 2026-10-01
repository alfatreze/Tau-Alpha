/* Builds sliders / selects / toggles from a meter's params[]; honours `when`; preset dropdown. Browser only. onChange(paramsObject). */
(function (root) {
  function build(host, meter, onChange) {
    host.innerHTML = '';
    const cur = Object.assign({}, ...meter.params.map((x) => ({ [x.key]: x.default })));
    const rows = {}, helps = {};
    const fire = () => { for (const k in helps) if (!helps[k][0].hidden) helps[k][1](); for (const x of meter.params) rows[x.key].style.display = Object.entries(x.when || {}).every(([k, v]) => cur[k] === v) ? '' : 'none'; onChange(Object.assign({}, cur)); };
    if (meter.presets.length) {
      const row = document.createElement('label'); row.textContent = 'Preset ';
      const sel = document.createElement('select');
      meter.presets.forEach((pr, i) => { const o = document.createElement('option'); o.value = i; o.textContent = pr.name; sel.appendChild(o); });
      sel.value = meter.default_preset;
      sel.onchange = () => { Object.assign(cur, meter.presets[sel.value].values); sync(); fire(); };
      row.appendChild(sel); host.appendChild(row);
    }
    const inputs = {};
    for (const x of meter.params) {
      const row = document.createElement('label'); row.textContent = x.label + ' '; rows[x.key] = row;
      let inp;
      if (x.type === 'enum') { inp = document.createElement('select'); x.values.forEach((v, i) => { const o = document.createElement('option'); o.value = i; o.textContent = v; inp.appendChild(o); }); }
      else if (x.type === 'bool') { inp = document.createElement('input'); inp.type = 'checkbox'; }
      else { inp = document.createElement('input'); inp.type = 'range'; inp.min = x.min; inp.max = x.max; inp.step = x.step; }
      const out = document.createElement('span'); out.className = 'val'; let extra = [];
      if (x.help) {                                   // (i): click or Enter toggles an explanation; enum choices get their own line
        const b = document.createElement('button'); b.type = 'button'; b.className = 'info'; b.textContent = 'i'; b.setAttribute('aria-label', 'About ' + x.label); b.setAttribute('aria-expanded', 'false');
        const hp = document.createElement('div'); hp.className = 'help'; hp.hidden = true;
        const draw = () => { hp.textContent = ''; const a = document.createElement('div'); a.textContent = x.help; hp.appendChild(a);
          if (x.value_help) x.value_help.forEach((t, i) => { const d = document.createElement('div'); d.textContent = t; if (i === cur[x.key]) d.className = 'sel'; hp.appendChild(d); }); };
        b.onclick = (e) => { e.preventDefault(); e.stopPropagation(); hp.hidden = !hp.hidden; b.setAttribute('aria-expanded', String(!hp.hidden)); if (!hp.hidden) draw(); };
        extra = [b, hp]; helps[x.key] = [hp, draw];
      }
      inp.oninput = inp.onchange = () => { cur[x.key] = x.type === 'bool' ? (inp.checked ? 1 : 0) : Number(inp.value); out.textContent = x.type === 'enum' ? x.values[cur[x.key]] : cur[x.key] + (x.unit || ''); fire(); };
      inputs[x.key] = [inp, out, x]; row.appendChild(inp); row.appendChild(out); extra.forEach((e) => row.appendChild(e)); host.appendChild(row);
    }
    function sync() { for (const [k, [inp, out, x]] of Object.entries(inputs)) { if (x.type === 'bool') inp.checked = !!cur[k]; else inp.value = cur[k]; out.textContent = x.type === 'enum' ? x.values[cur[k]] : cur[k] + (x.unit || ''); } }
    sync(); fire();
  }
  root.TauControls = { build };
})(typeof globalThis !== 'undefined' ? globalThis : window);
