// Cloud sync section of the start screen (#55).  Self-contained: it adds its own
// block above #status, and index.html calls ZelSync.apply() just before callMain.
//
// The port reads sync / sync_base / sync_key from zeliard.ini (IDBFS, /libsdl).
// This script keeps the player's choice in localStorage (usable before the
// engine has booted) and writes the three keys into the ini at start.  The
// server is this page's own origin, offered only when GET /api/health answers
// {"ok": true}; elsewhere (GitHub Pages) the section says there is no server and
// the ini is left alone.  Keys follow the emulated web build's rules: 4-32
// letters/digits, shown as ABCD-EFGH.
(function () {
  'use strict';
  const LS = 'zeliard.cloud';
  const INI = '/libsdl/awkto/zeliard/zeliard.ini';
  let avail = false;

  const load = () => { try { return JSON.parse(localStorage.getItem(LS)) || {}; } catch (e) { return {}; } };
  const save = v => { try { localStorage.setItem(LS, JSON.stringify(v)); } catch (e) { /* private window */ } };

  function normalize(s) {                                  // the same rule as the web app and the port
    const c = String(s || '').toUpperCase().replace(/[^A-Z0-9]/g, '');
    if (c.length < 4 || c.length > 32) return '';
    return c.match(/.{1,4}/g).join('-');
  }
  function newKey() {
    const al = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789', r = new Uint8Array(4);
    crypto.getRandomValues(r);
    return [...r].map(b => al[b % al.length]).join('');
  }

  const box = document.createElement('div');
  box.id = 'cloudsync';
  box.style.cssText = 'margin:1.2em 0;padding:.6em .9em;border:1px solid #444;border-radius:6px';
  box.innerHTML =
    '<b>Cloud sync</b> <small id="cs-note">checking for a save server...</small>' +
    '<div id="cs-body" style="display:none">' +
    '<label style="margin:.6em 0 .2em"><input id="cs-on" type="checkbox"> Sync my town saves (.usr) with the cloud</label>' +
    '<div><input id="cs-key" size="22" maxlength="40" placeholder="sync key, e.g. K7QF" autocomplete="off" spellcheck="false"' +
    ' style="text-transform:uppercase"> <button id="cs-link">Link</button> <button id="cs-new">New key</button></div>' +
    '<small id="cs-msg"></small>' +
    '<div><small>Use the same key as the "Server sync" card of the emulated web build to share saves with it. ' +
    'Anyone who has the key can read and replace the saves, so keep it private. ' +
    'If both sides already have a save the game asks which to keep before anything is replaced.</small></div>' +
    '</div>';
  const $ = id => box.querySelector('#' + id);
  const anchor = document.getElementById('status');
  anchor.parentNode.insertBefore(box, anchor);

  function render() {
    const s = load();
    $('cs-on').checked = !!s.on && !!s.key;
    $('cs-key').value = s.key || '';
    $('cs-msg').textContent = s.key
      ? (s.on ? 'Linked: key ' + s.key + '. Syncs when the game starts and after each save.' : 'Key ' + s.key + ' is saved; sync is off.')
      : 'Not linked yet: enter a key and press Link, or make a new one.';
  }
  function link(raw) {
    const key = normalize(raw);
    if (!key) { $('cs-msg').textContent = 'A sync key is 4 to 32 letters or digits.'; return; }
    save({ on: true, key });
    render();
  }
  $('cs-link').onclick = () => link($('cs-key').value);
  $('cs-key').onkeydown = e => { if (e.key === 'Enter') link($('cs-key').value); e.stopPropagation(); };
  $('cs-new').onclick = () => link(newKey());
  $('cs-on').onchange = e => {
    const s = load();
    if (e.target.checked && !s.key) { link($('cs-key').value || newKey()); return; }
    save({ ...s, on: e.target.checked });
    render();
  };

  // is there a save server on this origin?
  (async () => {
    const ctl = new AbortController(), t = setTimeout(() => ctl.abort(), 4000);
    try {
      const r = await fetch('/api/health', { cache: 'no-store', signal: ctl.signal });
      const j = r.ok ? await r.json() : null;
      avail = !!(j && j.ok === true);
    } catch (e) { avail = false; }
    clearTimeout(t);
    $('cs-note').textContent = avail ? '' : 'No save server is reachable from this site, so sync is off here.';
    $('cs-body').style.display = avail ? 'block' : 'none';
    if (avail) render();
  })();

  // Called by index.html just before callMain: write the port's settings.
  function apply() {
    if (!avail) return;                                    // nothing to say; the ini is left as it is
    const FS = Module.FS, s = load();
    let lines = [];
    try { lines = new TextDecoder().decode(FS.readFile(INI)).split('\n'); } catch (e) { /* first run */ }
    lines = lines.filter(l => l !== '' && !/^\s*(sync|sync_base|sync_key)\s*=/.test(l));
    if (s.key) {
      lines.push('sync=' + (s.on ? 1 : 0), 'sync_base=' + location.origin, 'sync_key=' + s.key);
    }
    try { FS.mkdirTree(INI.replace(/\/[^/]*$/, '')); } catch (e) { /* exists */ }
    FS.writeFile(INI, lines.join('\n') + '\n');
  }
  window.ZelSync = { apply, normalize };
})();
