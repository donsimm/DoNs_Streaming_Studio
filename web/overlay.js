// Zweiter Zeichner (P10, Teil B): zeichnet das Overlay, das der Kern schickt, wie der Zeichner
// des Editors (src/render/sourcerenderer.cpp, K-07). Ohne Bibliotheken.
//
// Daten vom Kern (Server-Sent Events «overlay», bei jeder Änderung das ganze Overlay, K-02):
//   { overlay: <Overlay im Format der Datei, docs/06-overlay-format.md>,
//     media: { "media/…": { orientation: 1..8 } },   // EXIF bzw. Display-Matrix
//     fonts: { "media/…": { units_per_em, ascent, descent, leading } } }
// «gone»: Die Adresse gilt nicht mehr (neuer Token, neue Adresse, gelöscht) → neu laden; der
// Server liefert dann die leere Seite.
//
// Vorgaben wie im Editor (docs/01-festlegungen.md, «Zeichnen im Editor»):
//   - Quellen in Zeichenreihenfolge (sources[0] ganz hinten), nur sichtbare, ganze Pixel.
//   - Kasten um die Mitte: zuerst spiegeln, dann drehen («rotate(θ) scale(fx, fy)»; CSS wendet
//     die rechte Funktion zuerst an, https://drafts.csswg.org/css-transforms/#transform-rendering).
//   - Bild und Video: Ausrichtung (EXIF/Display-Matrix: zuerst waagrecht spiegeln, dann drehen)
//     → gestreckt in den Kasten. Die Angaben in den Dateien setzt der Server auf «keine Drehung»
//     (src/server/mediapatch.h), damit das Ergebnis nicht von der Chromium-Version abhängt.
//   - Text: dieselbe Schriftdatei (FontFace), Zeilen nur an «\n», erste Grundlinie = Oberlänge,
//     danach je Zeilenabstand (Oberlänge + Unterlänge + Abstand, gerundet wie Qt auf 1/64 Pixel),
//     waagrecht ausgerichtet, am Kasten abgeschnitten (SVG).
//   - Video: stumm; beginnt von vorne beim Laden der Seite, beim Einblenden der Quelle, beim
//     Einschalten der Ausgabe und bei neuer Datei, «Wiederholen» oder «Am Ende anhalten»; Ende:
//     loop = wieder von vorne, hold_at_end = letztes Bild bleibt, sonst leer.
//   - Ausgabe aus (E-24): nichts zeigen, verbunden bleiben.
//
// Ton der Videos (P12b b4, Plan docs/archiv/prompts/P12-ton.md): Nur in OBS (window.obsstudio)
// meldet die Seite dem Kern Start und Lage jedes Videos (POST «clock», Format in
// src/server/clockreport.h) und öffnet die Ereignisse als «events/<Kennung der Seite>». Ohne
// window.obsstudio bleibt alles wie oben, ohne Meldungen.
//   - «aktiv» (obsSourceActiveChanged mit detail.active, Quelle im Programm): Lauf + 1, alle Videos
//     von vorne; je Video beim ersten Bild «start», danach etwa jede Sekunde «frame», beim ersten
//     Bild nach dem Wiederholen «loop», am Ende ohne Wiederholen «end».
//   - «inaktiv»: alle Videos anhalten und zurück auf den Anfang, «stop» (Entscheid des Nutzers
//     2026-11-13: verborgene Videos laufen nicht weiter). Neue Videos warten dann am Anfang.
//   - Neue Datei (oder Wiederholen/Anhalten geändert): neues Element, von vorne, mit «start»
//     (Entscheid des Nutzers 2026-11-13).
//   - Bis zum ersten Ereignis ist der Zustand unbekannt: Videos laufen wie ohne OBS, ohne Meldungen.
//     Beim Laden einer schon aktiven Quelle meldet OBS kein «aktiv» (M3, 2026-11-13). Rückfall: Der
//     Kern fragt OBS beim Beginn von «events/<Kennung>» (GetSourceActive) und schickt «obs_active»
//     ({active}); die Seite nimmt es wie «aktiv»/«inaktiv», aber nur bei unbekanntem Zustand.
//     Ohne Verbindung zu OBS schickt der Kern nichts, der Zustand bleibt unbekannt.
//
// Quellen:
//   EventSource: https://html.spec.whatwg.org/multipage/server-sent-events.html
//   FontFace: https://drafts.csswg.org/css-font-loading/
//   HTMLMediaElement (muted, loop, ended, error): https://html.spec.whatwg.org/multipage/media.html
//   SVG text, text-anchor: https://www.w3.org/TR/SVG2/text.html
//   window.obsstudio, obsSourceActiveChanged: https://github.com/obsproject/obs-browser/blob/master/README.md
//   requestVideoFrameCallback (mediaTime, expectedDisplayTime): https://wicg.github.io/video-rvfc/
//   fetch mit keepalive, Content-Type text/plain ohne Vorabfrage: https://fetch.spec.whatwg.org/
//   Crypto.getRandomValues: https://w3c.github.io/webcrypto/#Crypto-method-getRandomValues
'use strict';

const SVG = 'http://www.w3.org/2000/svg';
const root = document.getElementById('overlay');
const shown = new Map();     // Kennung der Quelle → { kind, key, box, inner }
const fontFaces = new Map(); // Name der Schrift → { face, state: 'loading'|'ok'|'error' }
let lastData = null;

// Zustand für die Prüfung (Testprogramm liest ihn mit runJavaScript).
window.dssState = { applied: 0, lastApplyMs: 0, output: null, connected: false, reports: [] };

function report(text) {
  window.dssState.reports.push(text);
  fetch('report', { method: 'POST', body: text }).catch(() => {});
}

// Ton der Videos, nur in OBS. active: null = unbekannt, true = aktiv, false = inaktiv.
const obs = typeof window.obsstudio !== 'undefined';
const clock = { page: obs ? newPageId() : '', seq: 0, run: 0, active: null };
window.dssState.page = clock.page;
window.dssState.active = null;

// Kennung der Seite, [a-z0-9]{12} (validClockPage im Server)
function newPageId() {
  const bytes = new Uint8Array(12);
  crypto.getRandomValues(bytes);
  return Array.from(bytes, (b) => (b % 36).toString(36)).join('');
}

function sendClock(fields) {
  const body = Object.assign({ v: 1, page: clock.page, seq: clock.seq, run: clock.run }, fields,
                             { now_ms: performance.now() });
  clock.seq += 1;
  // text/plain: keine Vorabfrage (CORS-safelisted); keepalive: kommt auch beim Schliessen an
  fetch('clock', { method: 'POST', body: JSON.stringify(body), keepalive: true,
                   headers: { 'Content-Type': 'text/plain' } }).catch(() => {});
}

function videoFields(v, event) {
  const fields = { event, source: v.dssSource };
  if (Number.isFinite(v.duration) && v.duration > 0)
    fields.duration_s = v.duration;
  return fields;
}

// Zähler eines Videos für den laufenden Lauf. Mit seek: zurück auf den Anfang und erst nach
// «seeked» zählen (sonst kommt noch ein Bild von vor dem Zurückspulen als «start»).
function resetClock(v, seek) {
  const c = { run: clock.run, started: false, loop: 0, lastMedia: 0, lastReportMs: 0, ready: true };
  v.dssClock = c;
  if (!seek)
    return;
  // Ohne geladene Daten (HAVE_NOTHING) gibt es kein Zurückspulen und kein «seeked».
  c.ready = v.readyState === HTMLMediaElement.HAVE_NOTHING;
  v.addEventListener('seeked', () => { c.ready = true; }, { once: true });
  v.currentTime = 0;
}

// Bild für Bild (requestVideoFrameCallback): mediaTime und expectedDisplayTime gehören zum selben
// Bild. In OBS kommen nur etwa 20 Aufrufe je Sekunde bei 30 Bildern/s (M3); «frame» braucht einen
// je Sekunde, und «start»/«loop» tragen die Lage des gemeldeten Bildes, auch wenn es nicht das
// allererste ist.
function onFrame(v, meta) {
  if (!v.isConnected)
    return; // entfernt: keine weiteren Aufrufe
  v.requestVideoFrameCallback((now, m) => onFrame(v, m));
  const c = v.dssClock;
  if (clock.active !== true || v.paused || v.seeking || !c.ready || c.run !== clock.run)
    return;
  let event = null;
  if (!c.started) {
    c.started = true;
    event = 'start';
  } else if (meta.mediaTime < c.lastMedia) {
    c.loop += 1;
    event = 'loop';
  } else if (meta.expectedDisplayTime - c.lastReportMs >= 1000) {
    event = 'frame';
  }
  c.lastMedia = meta.mediaTime;
  if (!event)
    return;
  c.lastReportMs = meta.expectedDisplayTime;
  sendClock(Object.assign(videoFields(v, event), { file: v.dssFile, loop: c.loop, media_s: meta.mediaTime,
                                                   display_ms: meta.expectedDisplayTime }));
}

function playVideo(v) {
  const p = v.play();
  if (p)
    p.catch(() => {});
}

// «aktiv»/«inaktiv» aus OBS: alle Videos zurück auf den Anfang, bei «aktiv» abspielen.
function setActive(active) {
  if (active === clock.active)
    return;
  clock.active = active;
  window.dssState.active = active;
  if (active)
    clock.run += 1;
  for (const e of shown.values()) {
    if (e.kind !== 'video' || e.inner.dssFailed)
      continue;
    const v = e.inner;
    v.pause();
    resetClock(v, true);
    v.style.visibility = '';
    if (active)
      playVideo(v);
  }
  if (!active)
    sendClock({ event: 'stop' });
}

if (obs)
  window.addEventListener('obsSourceActiveChanged', (event) => setActive(!!event.detail.active));

// Wie dss::fontMetricPixels (QFixed, gerundete Division).
function metricPixels(units, px, unitsPerEm) {
  const val = units * px * 64;
  const b = unitsPerEm * 64;
  return Math.trunc((val * 64 + Math.trunc(b / 2)) / b) / 64;
}

// Wie dss::orientationSteps: zuerst waagrecht spiegeln, dann im Uhrzeigersinn drehen.
function orientationSteps(o) {
  switch (o) {
    case 2: return { mirror: true, rotation: 0 };
    case 3: return { mirror: false, rotation: 180 };
    case 4: return { mirror: true, rotation: 180 };
    case 5: return { mirror: true, rotation: 270 };
    case 6: return { mirror: false, rotation: 90 };
    case 7: return { mirror: true, rotation: 90 };
    case 8: return { mirror: false, rotation: 270 };
    default: return { mirror: false, rotation: 0 };
  }
}

function fontName(t) {
  return 'dss-' + t.font_file.replace(/[^a-z0-9]/gi, '') + '-' + t.weight + (t.italic ? '-i' : '-n');
}

// Schriftdatei als FontFace mit genau dem gewählten Gewicht und Stil: Chromium nimmt dann diese
// Datei ohne Ersatz und setzt bei variablen Schriften die Achse wght auf das Gewicht.
function ensureFont(t) {
  const name = fontName(t);
  let entry = fontFaces.get(name);
  if (entry)
    return entry;
  const face = new FontFace(name, 'url("' + t.font_file + '")',
                            { weight: String(t.weight), style: t.italic ? 'italic' : 'normal' });
  entry = { face, state: 'loading' };
  fontFaces.set(name, entry);
  face.load().then(() => {
    document.fonts.add(face);
    entry.state = 'ok';
    if (lastData)
      apply(lastData);
  }, () => {
    entry.state = 'error';
    report('Schrift nicht lesbar: ' + t.font_file);
  });
  return entry;
}

function placeBox(box, s) {
  box.style.left = s.x + 'px';
  box.style.top = s.y + 'px';
  box.style.width = s.width + 'px';
  box.style.height = s.height + 'px';
  box.style.transform = 'rotate(' + s.rotation / 10 + 'deg) scale(' + (s.flip_horizontal ? -1 : 1) +
                        ', ' + (s.flip_vertical ? -1 : 1) + ')';
}

function placeMedia(el, s, orientation) {
  const steps = orientationSteps(orientation);
  const swap = steps.rotation % 180 !== 0;
  const w = swap ? s.height : s.width;
  const h = swap ? s.width : s.height;
  el.style.width = w + 'px';
  el.style.height = h + 'px';
  el.style.left = (s.width - w) / 2 + 'px';
  el.style.top = (s.height - h) / 2 + 'px';
  el.style.transform = 'rotate(' + steps.rotation + 'deg) scale(' + (steps.mirror ? -1 : 1) + ', 1)';
}

function createVideo(s) {
  const v = document.createElement('video');
  v.muted = true;
  v.defaultMuted = true;
  v.playsInline = true;
  v.preload = 'auto';
  v.loop = s.video.loop;
  // In OBS und inaktiv: am Anfang warten bis «aktiv»
  const wait = obs && clock.active === false;
  v.autoplay = !wait;
  v.addEventListener('ended', () => {
    // Ohne «Am Ende anhalten» wird die Quelle leer; mit bleibt das letzte Bild.
    if (!v.loop && !v.dssHoldAtEnd)
      v.style.visibility = 'hidden';
    if (obs && clock.active === true && v.dssClock.started && v.dssClock.run === clock.run)
      sendClock(videoFields(v, 'end'));
  });
  v.addEventListener('error', () => {
    v.style.visibility = 'hidden';
    v.dssFailed = true;
    const e = v.error;
    report('Video nicht abspielbar: «' + s.name + '» (' + s.video.file + ')' + (e ? ' (Fehler ' + e.code + ' ' + e.message + ')' : ''));
  });
  v.dssHoldAtEnd = s.video.hold_at_end;
  v.dssSource = s.id;
  v.dssFile = s.video.file;
  if (obs) {
    resetClock(v, false);
    v.requestVideoFrameCallback((now, m) => onFrame(v, m));
  }
  v.src = s.video.file;
  if (!wait)
    playVideo(v);
  return v;
}

function renderText(svg, s, metrics) {
  while (svg.firstChild)
    svg.removeChild(svg.firstChild);
  svg.setAttribute('width', s.width);
  svg.setAttribute('height', s.height);
  const t = s.text;
  const entry = ensureFont(t);
  if (entry.state !== 'ok' || !metrics)
    return; // wie im Editor: ohne lesbare Schrift kein Text
  const px = t.font_size;
  const ascent = metricPixels(metrics.ascent, px, metrics.units_per_em);
  const lineSpacing = ascent + metricPixels(metrics.descent, px, metrics.units_per_em) +
                      metricPixels(metrics.leading, px, metrics.units_per_em);
  const anchor = t.align === 'center' ? 'middle' : t.align === 'right' ? 'end' : 'start';
  const x = t.align === 'center' ? s.width / 2 : t.align === 'right' ? s.width : 0;
  let baseline = ascent;
  for (const line of t.content.split('\n')) {
    const el = document.createElementNS(SVG, 'text');
    el.setAttribute('x', x);
    el.setAttribute('y', baseline);
    el.setAttribute('text-anchor', anchor);
    el.setAttribute('fill', t.color);
    el.style.fontFamily = '"' + fontName(t) + '"';
    el.style.fontSize = px + 'px';
    el.style.fontWeight = String(t.weight);
    el.style.fontStyle = t.italic ? 'italic' : 'normal';
    el.textContent = line;
    svg.appendChild(el);
    baseline += lineSpacing;
  }
}

function sourceKey(s) {
  if (s.kind === 'video')
    return 'video|' + s.video.file + '|' + s.video.loop + '|' + s.video.hold_at_end;
  if (s.kind === 'image')
    return 'image|' + s.image.file;
  return 'text';
}

function removeShown(id) {
  const e = shown.get(id);
  if (!e)
    return;
  if (e.kind === 'video') {
    e.inner.pause();
    e.inner.removeAttribute('src');
    e.inner.load();
  }
  e.box.remove();
  shown.delete(id);
}

function apply(data) {
  lastData = data;
  const o = data.overlay;
  root.style.width = o.width + 'px';
  root.style.height = o.height + 'px';
  window.dssState.output = o.output;
  const wanted = o.output ? o.sources.filter((s) => s.visible) : [];
  const ids = new Set(wanted.map((s) => s.id));
  // Verborgene, gelöschte Quellen und Ausgabe aus: weg (Videos beginnen beim Einblenden neu).
  for (const id of [...shown.keys()]) {
    if (!ids.has(id))
      removeShown(id);
  }
  const ordered = [];
  for (const s of wanted) {
    let e = shown.get(s.id);
    const key = sourceKey(s);
    // Neue Datei, anderes Wiederholen/Anhalten oder abgebrochenes Video: neu, von vorne.
    if (e && (e.key !== key || (e.kind === 'video' && e.inner.dssFailed)))
      removeShown(s.id), (e = undefined);
    if (!e) {
      const box = document.createElement('div');
      box.className = 'source';
      let inner;
      if (s.kind === 'image') {
        inner = document.createElement('img');
        inner.alt = '';
        inner.addEventListener('error', () => report('Bild nicht lesbar: «' + s.name + '» (' + s.image.file + ')'));
        inner.src = s.image.file;
      } else if (s.kind === 'video') {
        inner = createVideo(s);
      } else {
        inner = document.createElementNS(SVG, 'svg');
      }
      box.appendChild(inner);
      e = { kind: s.kind, key, box, inner };
      shown.set(s.id, e);
    }
    placeBox(e.box, s);
    if (s.kind === 'image' || s.kind === 'video') {
      const file = s.kind === 'image' ? s.image.file : s.video.file;
      const info = data.media[file];
      placeMedia(e.inner, s, info ? info.orientation : 1);
    } else {
      renderText(e.inner, s, data.fonts[s.text.font_file]);
    }
    ordered.push(e);
  }
  // Zeichenreihenfolge: nur umstellen, was nicht schon an seiner Stelle steht. Verschobene Videos
  // hält der Browser an; sie laufen danach weiter (wie im Editor, kein Neubeginn).
  for (let i = 0; i < ordered.length; ++i) {
    if (root.children[i] === ordered[i].box)
      continue;
    const v = ordered[i].kind === 'video' ? ordered[i].inner : null;
    const playing = v && !v.paused && !v.ended;
    root.insertBefore(ordered[i].box, root.children[i] || null);
    if (playing)
      playVideo(v);
  }
  window.dssState.applied += 1;
  window.dssState.lastApplyMs = performance.now();
}

// Verbindung für Änderungen. EventSource verbindet sich nach einem Abbruch selbst neu («retry»
// vom Server); ist sie endgültig zu (readyState CLOSED), baut die Seite eine neue auf. Bis dahin
// bleibt das letzte Bild stehen.
function connect() {
  const source = new EventSource(obs ? 'events/' + clock.page : 'events');
  source.addEventListener('open', () => { window.dssState.connected = true; });
  source.addEventListener('overlay', (event) => apply(JSON.parse(event.data)));
  // Rückfall des Kerns (GetSourceActive): nur solange der Zustand unbekannt ist; ein Ereignis aus
  // OBS hat Vorrang.
  source.addEventListener('obs_active', (event) => {
    if (obs && clock.active === null)
      setActive(!!JSON.parse(event.data).active);
  });
  source.addEventListener('gone', () => {
    source.close();
    location.reload();
  });
  source.addEventListener('error', () => {
    window.dssState.connected = false;
    if (source.readyState === EventSource.CLOSED)
      setTimeout(connect, 1000);
  });
}

connect();
