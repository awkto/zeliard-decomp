// keys.js: keyboard handling for the web build.  Self-contained; index.html only
// includes it.  Browsers own some keys (tab switching on Ctrl/Alt+1..9, F1 help,
// F3 find, F5/F7/F12, the Alt menu bar...).  While the game is running this
//  1. calls preventDefault() on every key the game binds, so the browser does not
//     act on it (SDL still sees the event: we never stop propagation), and
//  2. asks for the Keyboard Lock API while the game is fullscreen, which also
//     releases the keys a page cannot otherwise claim (Ctrl+W, Ctrl+1..9 tab
//     switching, Ctrl+T/N, Alt+Tab-like shortcuts where the OS allows it).
// Keys no page can claim (Ctrl+W, Ctrl+T, Ctrl+N outside fullscreen) have working
// alternatives in the game itself: see "Web build" in port/README.md.
(function () {
  'use strict';
  const stage = document.getElementById('stage');
  const running = () => stage && stage.style.display === 'flex';
  const stats = { prevented: 0, last: '' };
  window.zelKeys = stats;                         // read by the browser test

  const digit = c => /^(Digit|Numpad)[1-9]$/.test(c);
  // does the game (or the browser, in a way that hurts) want this key?
  function claimed(e) {
    const c = e.code;
    if (c === 'AltLeft' || c === 'AltRight') return true;           // magic key; a bare Alt press shows the menu bar on Windows/Firefox
    if (/^F([1-9]|1[0-2])$/.test(c) && c !== 'F5') return true;      // F1 help, F3 find, F7 caret browsing, F11 fullscreen, F12 devtools
    if (c === 'Backspace' || c === 'Tab' || c === 'Space' || c.startsWith('Arrow')) return true;
    if ((e.ctrlKey || e.metaKey) && (digit(c) || c === 'Period' || c === 'Backslash')) return true;   // slot save, quick save/load
    if (e.altKey && digit(c)) return true;                           // slot load
    if (e.ctrlKey && !e.altKey && (c === 'KeyS' || c === 'KeyP' || c === 'KeyF' || c === 'KeyG' || c === 'KeyD')) return true;
    return false;
  }
  function onKey(e) {
    if (!running() || (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName))) return;
    if (!claimed(e)) return;
    e.preventDefault();
    stats.prevented++; stats.last = e.type + ' ' + e.code + (e.ctrlKey ? ' ctrl' : '') + (e.altKey ? ' alt' : '') + (e.shiftKey ? ' shift' : '');
  }
  addEventListener('keydown', onKey, true);
  addEventListener('keyup', onKey, true);          // Alt's menu bar opens on keyup

  // Fullscreen belongs to the page, not to SDL: the stage element goes fullscreen, the canvas keeps its
  // size and index.html's fit() letterboxes it.  F, F11 and Alt+Enter are handled here, in the key event
  // itself (a browser only grants fullscreen inside a user gesture, and SDL sees keys later).
  // Module.zelFs mirrors document.fullscreenElement for the port (main.c web_fs_state).
  function toggleFullscreen() {
    if (document.fullscreenElement) return document.exitFullscreen && document.exitFullscreen().catch(() => {});
    const el = document.getElementById('stage');
    if (el && el.requestFullscreen) return el.requestFullscreen().catch(() => {});
  }
  window.zelToggleFullscreen = toggleFullscreen;
  if (window.Module) window.Module.zelToggleFullscreen = toggleFullscreen;
  addEventListener('keydown', e => {
    if (!running() || e.repeat || (e.target && /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName))) return;
    const help = document.getElementById('help');
    if (help && !help.hidden) return;                                  // the controls overlay owns the keyboard
    const hit = (e.code === 'F11' || e.code === 'KeyF') && !e.ctrlKey && !e.altKey && !e.metaKey && !e.shiftKey ||
                (e.code === 'Enter' || e.code === 'NumpadEnter') && e.altKey && !e.ctrlKey && !e.metaKey;
    if (!hit) return;
    e.preventDefault(); e.stopImmediatePropagation();
    toggleFullscreen();
  }, true);

  // Keyboard Lock (Chromium; only while fullscreen).  Without it Ctrl+1..9 and Ctrl+W stay the browser's.
  function syncLock() {
    const kb = navigator.keyboard;
    if (!kb || !kb.lock) return;
    if (document.fullscreenElement) kb.lock().then(() => { stats.locked = true; }, () => { stats.locked = false; });
    else { kb.unlock(); stats.locked = false; }
  }
  document.addEventListener('fullscreenchange', syncLock);

  // Keep keyboard focus on the game (a click on the page margin would drop it).
  addEventListener('pointerdown', () => { if (running()) { const c = document.getElementById('canvas'); if (c) c.focus(); } }, true);
})();
