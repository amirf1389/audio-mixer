// Licence Manager: the admin console of the licence service. One page, no outside files, no cookies.
// The admin key is kept in this tab's session storage only and is sent in the Authorization header of each request.
"use strict";
const $app = document.getElementById("app");
const state = { key: sessionStorage.getItem("lkms.key") || "", tab: "overview", apps: [], filter: { app_id: "", status: "", q: "", offset: 0 }, limit: 25 };

// Elements are built with textContent, never with HTML strings: nothing a customer typed can run as code here.
function h(tag, attrs, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (k === "class") el.className = v; else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (v === true) el.setAttribute(k, ""); else if (v !== false && v != null) el.setAttribute(k, v);
  }
  for (const kid of kids.flat()) if (kid != null && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  return el;
}
async function api(method, path, body) {
  const r = await fetch(path, { method, headers: { Authorization: "Bearer " + state.key, ...(body ? { "Content-Type": "application/json" } : {}) }, body: body ? JSON.stringify(body) : undefined });
  let data = null; try { data = await r.json(); } catch (e) { /* no body */ }
  if (r.status === 401) {   // while signing in, the form shows the message itself; later it means the key stopped working: back to sign-in
    if (sessionStorage.getItem("lkms.key")) signOut();
    throw new Error("The admin key was not accepted."); }
  if (!r.ok) throw new Error(explain(data, r.status));
  return data;
}
const WORDS = { app_name_taken: "That app name is already used.", too_many_requests: "Too many requests. Wait a minute and try again.", seats_in_use: "More devices are activated than that limit. Remove devices first.",
  license_revoked_for_good: "A revoked licence cannot be changed. Issue a new key instead.", license_not_found: "Licence not found.", app_not_found: "App not found.", too_many_at_once: "At most 1000 keys at a time." };
function explain(data, status) {
  if (data && data.error) return WORDS[data.error] || data.error.replace(/_/g, " ");
  if (data && Array.isArray(data.detail)) return data.detail.map((d) => (d.loc || []).slice(1).join(".") + ": " + d.msg).join("; ");
  return "The server answered " + status + ".";
}
const when = (t) => t ? new Date(t).toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" }) : "–";
const day = (t) => t ? new Date(t).toLocaleDateString(undefined, { dateStyle: "medium" }) : "never";
const pill = (s) => h("span", { class: "pill " + s }, s);
function signOut() { state.key = ""; sessionStorage.removeItem("lkms.key"); render(); }
function copy(text, btn) { navigator.clipboard.writeText(text).then(() => { const t = btn.textContent; btn.textContent = "Copied"; setTimeout(() => (btn.textContent = t), 1200); }); }
function download(name, text) { const a = h("a", { href: URL.createObjectURL(new Blob([text], { type: "text/csv" })), download: name }); document.body.append(a); a.click(); a.remove(); }
function field(label, input) { return h("label", {}, label, input); }
function run(btn, errEl, fn) { return async () => { errEl.textContent = ""; btn.disabled = true; try { await fn(); } catch (e) { errEl.textContent = e.message; } finally { btn.disabled = false; } }; }

function render() {
  $app.replaceChildren();
  if (!state.key) return renderLogin();
  const tabs = [["overview", "Overview"], ["licences", "Licences"], ["apps", "Apps"], ["activity", "Activity"]];
  $app.append(h("header", {}, h("h1", {}, "Licence ", h("span", {}, "Manager")),
    h("nav", {}, tabs.map(([id, name]) => h("button", { class: state.tab === id ? "on" : "", onclick: () => { state.tab = id; render(); } }, name))),
    h("div", { class: "spacer" }), h("button", { class: "btn", onclick: signOut }, "Sign out")));
  const main = h("main", {}); $app.append(main);
  ({ overview: viewOverview, licences: viewLicences, apps: viewApps, activity: viewActivity })[state.tab](main).catch((e) => main.append(h("p", { class: "error" }, e.message)));
}

function renderLogin() {
  const input = h("input", { type: "password", autocomplete: "off", placeholder: "Admin key", "aria-label": "Admin key" }), err = h("div", { class: "error" });
  const btn = h("button", { class: "btn primary", type: "submit" }, "Sign in");
  const form = h("form", { class: "card login" }, h("h1", {}, "Licence Manager"), h("p", { class: "muted" }, "Enter the admin key. It stays in this browser tab and is forgotten when the tab closes."),
    input, h("div", { class: "gap" }, btn), err);
  form.addEventListener("submit", async (e) => { e.preventDefault(); err.textContent = ""; state.key = input.value.trim();
    try { await api("GET", "/admin/overview"); sessionStorage.setItem("lkms.key", state.key); render(); } catch (x) { state.key = ""; err.textContent = x.message; } });
  $app.append(form); input.focus();
}

async function loadApps() { state.apps = (await api("GET", "/admin/apps")).apps; return state.apps; }

async function viewOverview(main) {
  const o = await api("GET", "/admin/overview"), s = o.licenses_by_status;
  const tile = (n, label) => h("div", { class: "tile" }, h("b", {}, n), h("span", {}, label));
  main.append(h("div", { class: "tiles" }, tile(s.active, "Active licences"), tile(o.activations, "Activated devices"), tile(o.devices_seen_24h, "Devices seen in 24 hours"),
    tile(o.expiring_30_days, "Expire within 30 days"), tile(s.suspended, "Suspended"), tile(s.expired, "Expired"), tile(s.revoked, "Revoked"), tile(o.apps, "Apps")));
  const days = o.activity_7_days, max = Math.max(1, ...days.map((d) => d.activations));
  const bars = h("div", { class: "bars" }, days.map((d) => { const bar = h("i", { title: d.activations + " activations" }); bar.style.height = Math.round((d.activations / max) * 90) + "px";
    return h("div", {}, d.activations, bar, d.day.slice(5)); }));
  main.append(h("div", { class: "card" }, h("h2", {}, "New activations, last 7 days"), days.length ? bars : h("p", { class: "muted" }, "No activity in the last 7 days.")));
}

async function viewLicences(main) {
  const apps = await loadApps(), f = state.filter;
  // ---- issue new keys
  const appSel = h("select", {}, apps.filter((a) => a.status === "active").map((a) => h("option", { value: a.app_id }, a.name)));
  const count = h("input", { type: "number", min: 1, max: 1000, value: 1 }), plan = h("input", { value: "standard", maxlength: 60 }), seats = h("input", { type: "number", min: 1, max: 100000, value: 1 });
  const exp = h("input", { type: "date" }), email = h("input", { type: "email", placeholder: "optional" }), err = h("div", { class: "error" }), out = h("div", {});
  const site = h("input", { type: "number", min: 1, max: 1000000, placeholder: "device licence" });
  const issue = h("button", { class: "btn primary" }, "Create keys");
  issue.onclick = run(issue, err, async () => {
    const body = { app_id: appSel.value, count: Number(count.value), plan: plan.value.trim() || "standard", max_activations: Number(seats.value) };
    if (exp.value) body.expires_at = exp.value + "T23:59:59Z"; if (email.value.trim()) body.customer_email = email.value.trim();
    if (site.value) body.volume_seats = Number(site.value);
    const made = (await api("POST", "/admin/licenses", body)).licenses, keys = made.map((m) => m.license_key);
    const cp = h("button", { class: "btn" }, "Copy all"), dl = h("button", { class: "btn" }, "Download CSV");
    cp.onclick = () => copy(keys.join("\n"), cp);
    dl.onclick = () => download("licence-keys.csv", "license_key,plan,max_activations,expires_at,customer_email\n" + made.map((m) => [m.license_key, m.plan, m.max_activations, m.expires_at || "", m.customer_email || ""].join(",")).join("\n") + "\n");
    out.replaceChildren(h("div", { class: "notice" }, h("b", {}, keys.length + (keys.length === 1 ? " key created." : " keys created.") + " Save them now: they are not shown again."), h("pre", { class: "mono" }, keys.join("\n")), h("div", { class: "row" }, cp, dl)));
    list();
  });
  main.append(h("div", { class: "card" }, h("h2", {}, "Create licence keys"), apps.length ? h("div", { class: "row" }, field("App", appSel), field("How many", count), field("Plan", plan), field("Devices per key", seats), field("Expires", exp), field("Customer e-mail", email), field("Site seats (volume licence)", site), issue)
    : h("p", { class: "muted" }, "Register an app first, on the Apps tab."), err, out));
  // ---- find and list
  const fApp = h("select", {}, h("option", { value: "" }, "All apps"), apps.map((a) => h("option", { value: a.app_id, selected: f.app_id === a.app_id }, a.name)));
  const fStatus = h("select", {}, ["", "active", "suspended", "expired", "revoked"].map((s) => h("option", { value: s, selected: f.status === s }, s || "Any status")));
  const fQ = h("input", { type: "search", placeholder: "Whole key, start of a key, e-mail or plan", value: f.q }), body = h("tbody", {}), info = h("span", { class: "muted small" });
  const prev = h("button", { class: "btn" }, "Previous"), next = h("button", { class: "btn" }, "Next");
  async function list() {
    const p = new URLSearchParams({ limit: state.limit, offset: f.offset }); if (f.app_id) p.set("app_id", f.app_id); if (f.status) p.set("status", f.status); if (f.q) p.set("q", f.q);
    const r = await api("GET", "/admin/search/licenses?" + p);
    body.replaceChildren(...r.licenses.map((l) => h("tr", { class: "click", tabindex: 0, onclick: () => openLicence(l.license_id, list), onkeydown: (e) => { if (e.key === "Enter") openLicence(l.license_id, list); } },
      h("td", { class: "mono" }, l.key_hint), h("td", {}, l.app_name), h("td", {}, l.plan), h("td", {}, pill(l.status)), h("td", { class: "num" }, l.kind === "volume" ? l.volume_seats + " site seats" : l.activations + " / " + l.max_activations),
      h("td", {}, day(l.expires_at)), h("td", {}, l.customer_email || "–"), h("td", {}, day(l.created_at)))));
    if (!r.licenses.length) body.append(h("tr", {}, h("td", { colspan: 8, class: "muted" }, "No licences match.")));
    info.textContent = r.total ? (f.offset + 1) + "–" + (f.offset + r.licenses.length) + " of " + r.total : "";
    prev.disabled = f.offset === 0; next.disabled = f.offset + state.limit >= r.total;
  }
  const apply = () => { f.app_id = fApp.value; f.status = fStatus.value; f.q = fQ.value.trim(); f.offset = 0; list(); };
  fApp.onchange = fStatus.onchange = apply; fQ.onkeydown = (e) => { if (e.key === "Enter") apply(); };
  prev.onclick = () => { f.offset = Math.max(0, f.offset - state.limit); list(); }; next.onclick = () => { f.offset += state.limit; list(); };
  main.append(h("div", { class: "card" }, h("h2", {}, "Licences"), h("div", { class: "row" }, field("App", fApp), field("Status", fStatus), field("Search", fQ), h("button", { class: "btn", onclick: apply }, "Search")),
    h("div", { class: "tablewrap gap" }, h("table", {}, h("thead", {}, h("tr", {}, ["Key", "App", "Plan", "Status", "Devices", "Expires", "Customer", "Created"].map((t, i) => h("th", { class: i === 4 ? "num" : "" }, t)))), body)),
    h("div", { class: "pager" }, info, prev, next)));
  await list();
}

async function openLicence(id, refresh) {
  const shade = h("div", { class: "shade" }), drawer = h("aside", { class: "drawer", role: "dialog", "aria-label": "Licence" });
  const close = () => { shade.remove(); drawer.remove(); refresh && refresh(); }; shade.onclick = close;
  document.body.append(shade, drawer);
  async function draw() {
    const l = await api("GET", "/admin/license/" + id), dead = l.status === "revoked", err = h("div", { class: "error" });
    const plan = h("input", { value: l.plan, disabled: dead }), seats = h("input", { type: "number", min: 1, value: l.max_activations, disabled: dead });
    const exp = h("input", { type: "date", value: l.expires_at ? l.expires_at.slice(0, 10) : "", disabled: dead }), email = h("input", { type: "email", value: l.customer_email || "", disabled: dead });
    const save = h("button", { class: "btn primary", disabled: dead }, "Save changes");
    save.onclick = run(save, err, async () => { const b = { plan: plan.value.trim(), max_activations: Number(seats.value) };
      if (exp.value) b.expires_at = exp.value + "T23:59:59Z"; else b.never_expires = true; if (email.value.trim()) b.customer_email = email.value.trim();
      await api("PATCH", "/admin/license/" + id, b); await draw(); });
    const act = (label, action, cls, ask) => { const b = h("button", { class: "btn " + cls }, label);
      b.onclick = run(b, err, async () => { if (ask && !confirm(ask)) return; await api("PATCH", "/admin/license/" + id, { action }); await draw(); }); return b; };
    const actions = dead ? [] : [l.status === "suspended" ? act("Reinstate", "reinstate", "") : act("Suspend", "suspend", ""),
      act("Revoke for good", "revoke", "danger", "Revoke this licence? It stops working on every device and cannot be brought back.")];
    drawer.replaceChildren(h("div", { class: "row" }, h("div", {}, h("h2", { class: "mono" }, l.key_hint), h("div", { class: "muted small" }, l.app_name + " · created " + when(l.created_at))), h("div", { class: "spacer" }), pill(l.status), h("button", { class: "btn", onclick: close }, "Close")),
      h("p", { class: "small muted" }, l.status_reason ? "Reason: " + l.status_reason : ""),
      h("h3", {}, "Details"), h("div", { class: "row" }, field("Plan", plan), field("Devices allowed", seats)), h("div", { class: "row gap" }, field("Expires (empty: never)", exp), field("Customer e-mail", email)),
      h("div", { class: "row gap" }, save, ...actions), err,
      h("h3", {}, l.kind === "volume" ? "Site hosts (" + l.activations.length + " of " + l.max_activations + "), " + l.volume_seats + " seats each" : "Activated devices (" + l.activations.length + " of " + l.max_activations + ")"),
      l.activations.length ? h("div", { class: "tablewrap" }, h("table", {}, h("thead", {}, h("tr", {}, ["Device", "Address", "Last seen", ""].map((t) => h("th", {}, t)))), h("tbody", {}, l.activations.map((a) => {
        const rm = h("button", { class: "btn danger" }, "Remove");
        rm.onclick = run(rm, err, async () => { if (!confirm("Remove this device? Its seat becomes free.")) return; await api("DELETE", "/admin/activations/" + a.activation_id); await draw(); });
        return h("tr", {}, h("td", {}, a.device_name || h("span", { class: "mono" }, a.device)), h("td", { class: "mono" }, a.ip_address || "–"), h("td", {}, when(a.last_seen_at)), h("td", {}, rm)); })))) : h("p", { class: "muted" }, "No device is activated."),
      h("h3", {}, "History"), h("div", { class: "tablewrap" }, h("table", {}, h("tbody", {}, l.history.map((e) => h("tr", {}, h("td", {}, when(e.at)), h("td", {}, e.action), h("td", { class: "muted" }, e.actor.startsWith("app:") ? "app" : e.actor)))))));
  }
  try { await draw(); } catch (e) { drawer.replaceChildren(h("p", { class: "error" }, e.message), h("button", { class: "btn", onclick: close }, "Close")); }
}

async function viewApps(main) {
  const name = h("input", { placeholder: "App name", maxlength: 120 }), err = h("div", { class: "error" }), out = h("div", {}), body = h("tbody", {}), add = h("button", { class: "btn primary" }, "Register app");
  const secret = (a, title) => { const cp = h("button", { class: "btn" }, "Copy secret"); cp.onclick = () => copy(a.api_secret, cp);
    out.replaceChildren(h("div", { class: "notice" }, h("b", {}, title + " Save the secret now: it is not shown again."), h("pre", { class: "mono" }, "app_id:     " + a.app_id + "\napi_secret: " + a.api_secret), cp)); };
  async function list() {
    const apps = await loadApps();
    body.replaceChildren(...apps.map((a) => { const on = a.status === "active", tog = h("button", { class: "btn" }, on ? "Disable" : "Enable"), rot = h("button", { class: "btn danger" }, "New secret");
      tog.onclick = run(tog, err, async () => { if (on && !confirm("Disable " + a.name + "? Every copy of the app is refused until you enable it again.")) return; await api("PATCH", "/admin/apps/" + a.app_id, { status: on ? "disabled" : "active" }); await list(); });
      rot.onclick = run(rot, err, async () => { if (!confirm("Make a new secret for " + a.name + "? The old secret stops working at once.")) return; secret(await api("POST", "/admin/apps/" + a.app_id + "/rotate-secret"), "New secret made."); });
      return h("tr", {}, h("td", {}, a.name), h("td", { class: "mono" }, a.app_id), h("td", {}, pill(a.status)), h("td", { class: "num" }, a.licenses), h("td", { class: "num" }, a.activations), h("td", {}, day(a.created_at)), h("td", {}, h("div", { class: "row" }, tog, rot))); }));
    if (!apps.length) body.append(h("tr", {}, h("td", { colspan: 7, class: "muted" }, "No apps yet.")));
  }
  add.onclick = run(add, err, async () => { if (!name.value.trim()) throw new Error("Enter a name."); secret(await api("POST", "/admin/apps", { name: name.value.trim() }), "App registered."); name.value = ""; await list(); });
  main.append(h("div", { class: "card" }, h("h2", {}, "Register an app"), h("div", { class: "row" }, field("Name", name), add), err, out),
    h("div", { class: "card" }, h("h2", {}, "Apps"), h("div", { class: "tablewrap" }, h("table", {}, h("thead", {}, h("tr", {}, ["Name", "App id", "Status", "Licences", "Devices", "Created", ""].map((t, i) => h("th", { class: i === 3 || i === 4 ? "num" : "" }, t)))), body))));
  await list();
}

async function viewActivity(main) {
  const ev = (await api("GET", "/admin/audit?limit=200")).events;
  main.append(h("div", { class: "card" }, h("h2", {}, "Activity"), h("div", { class: "tablewrap" }, h("table", {}, h("thead", {}, h("tr", {}, ["When", "What", "Who", "Key", "App", "Address"].map((t) => h("th", {}, t)))),
    h("tbody", {}, ev.length ? ev.map((e) => h("tr", {}, h("td", {}, when(e.at)), h("td", {}, e.action), h("td", {}, e.actor.startsWith("app:") ? "app" : e.actor), h("td", { class: "mono" }, e.key_hint || "–"), h("td", {}, e.app_name || "–"), h("td", { class: "mono" }, e.ip_address || "–")))
      : h("tr", {}, h("td", { colspan: 6, class: "muted" }, "Nothing has happened yet.")))))));
}
render();
