/* LapLogic settings form - shared by the board's own page and the flasher.
   LapLogicForm(rootElement, { values, timezones, drivers }) builds the form and returns
   { changes(), setErrors(map), reset(values) }. Values are strings, keyed as in settings.cpp. */
// The grid, for choosing a favourite before LapLogic has loaded the current standings
// (it sends the up-to-date list once it has). Update at the start of each season.
const LAPLOGIC_GRID = [["ALB", "Albon"], ["ALO", "Alonso"], ["ANT", "Antonelli"], ["BEA", "Bearman"], ["BOR", "Bortoleto"],
  ["BOT", "Bottas"], ["COL", "Colapinto"], ["GAS", "Gasly"], ["HAD", "Hadjar"], ["HAM", "Hamilton"], ["HUL", "Hulkenberg"],
  ["LAW", "Lawson"], ["LEC", "Leclerc"], ["LIN", "Lindblad"], ["NOR", "Norris"], ["OCO", "Ocon"], ["PER", "Perez"],
  ["PIA", "Piastri"], ["RUS", "Russell"], ["SAI", "Sainz"], ["STR", "Stroll"], ["VER", "Verstappen"]];

function LapLogicForm(root, data) {
  "use strict";
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  let orig = Object.assign({}, data.values);
  const cur = Object.assign({}, data.values);
  const board = orig.board || "guition";
  const tz = data.timezones || [];
  let drivers = (data.drivers && data.drivers.length) ? data.drivers : LAPLOGIC_GRID;
  const rows = {};

  function el(html) { const t = document.createElement("template"); t.innerHTML = html.trim(); return t.content.firstElementChild; }

  function field(sec, key, label, hint, control) {
    const row = el(`<div class="field" data-key="${key}"><label>${esc(label)}${hint ? `<small>${hint}</small>` : ""}</label><div></div></div>`);
    row.lastElementChild.appendChild(control);
    sec.appendChild(row);
    rows[key] = row;
    return row;
  }
  function mark(key) {
    const r = rows[key];
    if (r) r.classList.toggle("changed", key === "ofPass" ? !!cur.ofPass : key === "ofClear" ? cur.ofClear === "1" : cur[key] !== orig[key]);
    const e = r && r.querySelector(".err"); if (e) e.remove();
    if (key === "rotOn") rows.rot && rows.rot.classList.toggle("disabled", cur.rotOn !== "1");
  }
  function select(key, options) {
    const s = el(`<select>${options.map(([v, t]) => `<option value="${esc(v)}">${esc(t)}</option>`).join("")}</select>`);
    if (![...s.options].some(o => o.value === cur[key]) && cur[key]) s.insertAdjacentHTML("afterbegin", `<option value="${esc(cur[key])}">${esc(cur[key])}</option>`);
    s.value = cur[key] || "";
    s.onchange = () => { cur[key] = s.value; mark(key); };
    return s;
  }
  function seg(key, options) {
    const g = el(`<div class="seg" role="group"></div>`);
    for (const [v, t] of options) {
      const b = el(`<button type="button" aria-pressed="${cur[key] === v}">${esc(t)}</button>`);
      b.onclick = () => { cur[key] = v; g.querySelectorAll("button").forEach(x => x.setAttribute("aria-pressed", String(x === b))); mark(key); };
      g.appendChild(b);
    }
    return g;
  }
  function toggle(key) {
    const b = el(`<button type="button" class="switch" role="switch" aria-checked="${cur[key] === "1"}"></button>`);
    b.onclick = () => { cur[key] = cur[key] === "1" ? "0" : "1"; b.setAttribute("aria-checked", String(cur[key] === "1")); mark(key); };
    return b;
  }
  function range(key, min, max, step, unit) {
    const w = el(`<div class="range"><input type="range" min="${min}" max="${max}" step="${step}" value="${esc(cur[key])}"><output>${esc(cur[key])}${unit}</output></div>`);
    const i = w.querySelector("input"), o = w.querySelector("output");
    i.oninput = () => { cur[key] = String(i.value); o.textContent = i.value + unit; mark(key); };
    return w;
  }
  function text(key, type, placeholder) {
    const i = el(`<input type="${type}" autocomplete="${type === "password" ? "new-password" : "off"}" spellcheck="false" placeholder="${esc(placeholder || "")}">`);
    i.value = key === "ofPass" ? "" : (cur[key] || "");
    i.oninput = () => { cur[key] = i.value.trim ? (type === "password" ? i.value : i.value.trim()) : i.value; mark(key); };
    return i;
  }
  function section(title, note) {
    const s = el(`<section><h3>${esc(title)}</h3>${note ? `<p class="note">${note}</p>` : ""}</section>`);
    root.appendChild(s);
    return s;
  }

  root.classList.add("llf");
  root.innerHTML = "";

  // ---- You
  let s = section("You");
  const driverOptions = (list) => [["", "None"]].concat(list.map(d => [d[0], `${d[0]}  ${d[1] || ""}`.trim()]));
  field(s, "fav", "Favourite driver", "Highlighted on every page", select("fav", driverOptions(drivers)));
  field(s, "tz", "Time zone", "Automatic works it out from your internet connection",
        select("tz", [["auto", "Automatic"]].concat(tz.map(z => [z, z.replace(/_/g, " ")]))));
  field(s, "h24", "Clock", "", seg("h24", [["0", "12-hour"], ["1", "24-hour"]]));
  field(s, "units", "Weather units", "", seg("units", [["metric", "°C, km/h"], ["imperial", "°F, mph"]]));

  // ---- Screen
  s = section("Screen");
  field(s, "orient", "Orientation", board === "cyd" ? "LapLogic restarts, then asks you to tap the calibration crosses" : "LapLogic restarts to lay out the screens",
        seg("orient", [["landscape", "Landscape"], ["portrait", "Portrait"]]));
  field(s, "theme", "Colour scheme", "", seg("theme", [["dark", "Dark mode"], ["light", "Light mode"]]));
  field(s, "bright", "Brightness", "", range("bright", 5, 100, 5, "%"));
  if (orig.autoDim !== undefined)
    field(s, "autoDim", "Auto-dim", "Follows the room's light, using the sensor beside the screen", toggle("autoDim"));
  if (orig.led !== undefined)
    field(s, "led", "Rear race light", "The light on the back: blue for practice, purple for qualifying, green for the race, amber just before the start", toggle("led"));
  field(s, "flip", "Upside down", "For stands that hold the board the other way up", toggle("flip"));
  field(s, "night", "Dim at night", "10 pm to 7 am; touching the screen brightens it", toggle("night"));
  if (board === "cyd") {
    field(s, "colours", "Display colour fix", "Only if black looks white on every page (some screens need it)", seg("colours", [["auto", "Automatic"], ["normal", "Normal"], ["inverted", "Inverted"]]));
    field(s, "panel", "Display type", "Only if red and blue look swapped. Restarts LapLogic",
          select("panel", [["auto", "Detect automatically"], ["ili9341", "ILI9341 (one USB port)"], ["st7789", "ST7789 (two USB ports)"]]));
  }

  // ---- Pages
  s = section("Auto scroll");
  field(s, "rotOn", "Auto scroll", "Steps through the main pages by itself", toggle("rotOn"));
  field(s, "rot", "Auto scroll time", "Seconds on each page", range("rot", 5, 120, 5, " s"));

  // ---- Updates
  if (orig.updCheck !== undefined) {
    s = section("Updates");
    field(s, "updCheck", "Check for updates", "Once a day, from LapLogic's GitHub releases. It always asks before installing", toggle("updCheck"));
  }

  // ---- Live track map (4.3" Guition only)
  if (board === "guition") {
    s = section("Live track map (OpenF1)",
      `Without a login, the Track page shows the circuit and a replay of each race's final lap. ` +
      `For cars moving live during sessions, add an <a href="https://openf1.org/auth.html" target="_blank" rel="noopener">OpenF1 sponsor login</a>.`);
    field(s, "ofUser", "OpenF1 email", "", text("ofUser", "email", "you@example.com"));
    field(s, "ofPass", "OpenF1 password", orig.ofPassSet === "1" ? "Saved. Leave blank to keep it" : "",
          text("ofPass", "password", orig.ofPassSet === "1" ? "••••••••" : ""));
    if (orig.ofPassSet === "1" || orig.ofUser) field(s, "ofClear", "Forget the login", "", toggle("ofClear"));
  }

  Object.keys(rows).forEach(mark);

  return {
    changes() {
      const out = {};
      for (const k of Object.keys(cur)) {
        if (k === "board" || k === "version" || k === "ofPassSet") continue;
        if (k === "ofPass") { if (cur.ofPass) out.ofPass = cur.ofPass; continue; }
        if (k === "ofClear") { if (cur.ofClear === "1") out.ofClear = "1"; continue; }
        if (cur[k] !== orig[k]) out[k] = cur[k];
      }
      return out;
    },
    setErrors(errs) {
      for (const [k, msg] of Object.entries(errs || {})) {
        const r = rows[k]; if (!r) continue;
        r.querySelectorAll(".err").forEach(e => e.remove());
        r.appendChild(el(`<div class="err">${esc(msg)}</div>`));
      }
    },
    // the current standings arrived: swap in that list, keeping the choice
    setDrivers(list) {
      if (!list || !list.length) return;
      drivers = list;
      const old = rows.fav.querySelector("select");
      const fresh = select("fav", driverOptions(drivers));
      old.replaceWith(fresh);
      mark("fav");
    },
    reset(values) { orig = Object.assign({}, values); Object.assign(cur, values); delete cur.ofPass; delete cur.ofClear; Object.keys(rows).forEach(mark); },
  };
}
