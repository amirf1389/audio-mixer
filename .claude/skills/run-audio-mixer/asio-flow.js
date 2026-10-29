(async () => {
  const wait = ms => new Promise(r => setTimeout(r, ms));
  const h = () => ({ state: document.getElementById('asio-hero-state').textContent, action: document.getElementById('asio-hero-action').textContent, hint: document.getElementById('asio-hero-hint').textContent, auto: document.getElementById('asio-auto-toggle').checked, storage: localStorage.getItem('halx_asio_auto') });
  const out = {};
  out.baseline = h();
  document.getElementById('asio-auto-toggle').click(); await wait(300);
  out.auto_off = h();
  document.getElementById('asio-hero-action').click(); await wait(1000);
  out.after_action = h();
  document.getElementById('asio-auto-toggle').click(); await wait(300);
  out.auto_on = h();
  return out;
})()
