<script>
  import { fade } from 'svelte/transition';
  import { createDemo } from './demo.js';
  import Chart from './Chart.svelte';

  const STALE_MS = 2000;  // a value older than this is shown greyed out
  const LOG_TAGS = ['ACK', 'ERR', 'CFR', 'PON', 'EVT'];
  const T_SOE_S = -45;  // must match flight-firmware/src/config.h
  const STS_S = ['ch1', 'ch2', 'ch3', 'ch4', 'baro', 'IMU'], STS_M = ['SD HK', 'SD IMU', 'flash', 'supply'];

  let pk = $state({});      // latest packet per tag: { d: packet, at: receive time }
  let events = $state([]);  // newest first
  let now = $state(Date.now());
  let lastAt = $state(0);
  let badLines = $state(0);
  let cfrAt = $state(0);
  let rebootAskAt = $state(0);  // in-page confirm: browser confirm() dialogs are not always available

  let mode = $state('demo');
  let url = $state('ws://localhost:8080');
  let link = $state(null);
  let cmdText = $state('');
  let ledLevel = $state(255);

  // Last minute of every live value, for the hover charts. Plain object: the charts redraw on the `now` tick.
  const HIST_MS = 60000;
  let hist = {};
  function rec(key, v, at) {
    const a = (hist[key] ??= []);
    a.push({ t: at, v: Array.isArray(v) ? v : [v] });
    while (a.length && a[0].t < at - HIST_MS) a.shift();
  }
  function record(p, at) {
    const C = { CH1: 0, CH2: 2 }[p.t];
    if (C != null) for (let k = 0; k < 2; k++) { rec(`ch${C + k}.p`, p.p[k], at); rec(`ch${C + k}.T`, p.T[k], at); }
    if (p.t === 'BAR') { rec('bar.p', p.p, at); rec('bar.T', p.T, at); }
    if (p.t === 'ACC') rec('acc', p.a, at);
    if (p.t === 'GYR') rec('gyr', p.g, at);
    if (p.t === 'HK') { rec('v', p.v, at); rec('ns', p.ns, at); }
    if (p.t === 'ACT') { p.led.forEach((v, i) => rec(`led${i}`, v, at)); rec('cmds', [p.rx, p.bad], at); }
    if (p.t === 'STA') { rec('soe', p.soe, at); rec('lo', p.lo, at); rec('campwr', p.cam, at); }
    if (p.t === 'CAM') p.rt.forEach((v, i) => rec(`cam${i}`, v < 0 ? null : v, at));
  }

  // name, unit, decimals, series names
  const META = {
    'bar.p': ['Mainboard pressure', 'hPa', 1], 'bar.T': ['Mainboard temperature', '°C', 1],
    acc: ['Acceleration', 'g', 2, ['x', 'y', 'z']], gyr: ['Angular rate', 'dps', 1, ['x', 'y', 'z']],
    v: ['5 V rail', 'V', 2], ns: ['Sensors OK', 'of 6', 0], cmds: ['Commands', '', 0, ['rx', 'bad']],
    soe: ['SOE', '', 0], lo: ['LO', '', 0], campwr: ['Camera power', '', 0],
  };
  for (let i = 0; i < 4; i++) {
    META[`ch${i}.p`] = [`Chamber ${i + 1} pressure`, 'hPa', 1];
    META[`ch${i}.T`] = [`Chamber ${i + 1} temperature`, '°C', 1];
    META[`led${i}`] = [`Chamber ${i + 1} LED`, '/ 255', 0];
  }
  for (let i = 0; i < 8; i++) META[`cam${i}`] = [`Cam ${i + 1} recording left`, 'min', 0];

  let hover = $state(null);  // { key, left, top }
  const POP_W = 320, POP_H = 170;
  function show(key, el) {
    const r = el.getBoundingClientRect();
    const left = Math.max(8, Math.min(r.left, innerWidth - POP_W - 8));
    let top = r.bottom + 6;
    if (top + POP_H > innerHeight) top = Math.max(8, r.top - POP_H - 6);
    hover = { key, left, top };
  }
  const hide = () => (hover = null);
  // spread onto any live value: {...tip('v')}. Mouse: hover. Touch: tap, tap elsewhere closes. Keyboard: focus.
  const tip = (key) => ({
    'data-tip': key,
    tabindex: 0,
    onpointerenter: (e) => e.pointerType !== 'touch' && show(key, e.currentTarget),
    onpointerleave: (e) => e.pointerType !== 'touch' && hide(),
    onclick: (e) => show(key, e.currentTarget),
    onfocus: (e) => show(key, e.currentTarget),
    onblur: hide,
  });

  setInterval(() => (now = Date.now()), 250);
  $effect(() => { if (!link) connect(); });  // open in demo mode so the page is never empty

  function log(t, text) {
    events = [{ at: Date.now(), t, text }, ...events].slice(0, 100);
  }

  // one JSON packet per line / per WebSocket message
  function onLine(line) {
    let p;
    try { p = JSON.parse(line); } catch { badLines++; return; }
    const at = Date.now();
    lastAt = at;
    pk[p.t] = { d: p, at };
    record(p, at);
    if (p.t === 'CFR') cfrAt = at;
    if (LOG_TAGS.includes(p.t)) log(p.t, p.cmd ?? p.err ?? p.fw ?? p.e ?? '');
  }

  function connect() {
    link?.close();
    pk = {};
    hist = {};
    if (mode === 'demo') {
      link = createDemo(onLine);
      log('LINK', 'demo started');
      return;
    }
    // Bridge contract: one packet per message in, one command line per message out
    const ws = new WebSocket(url);
    ws.onopen = () => log('LINK', 'connected ' + url);
    ws.onclose = () => log('LINK', 'disconnected');
    ws.onmessage = (e) => onLine(e.data);
    link = { send: (l) => ws.readyState === 1 && ws.send(l), close: () => ws.close() };
  }

  function send(cmd) {
    if (!link) return log('LINK', 'not connected');
    log('TX', cmd);
    link.send(cmd);
  }

  function sendText() {
    const c = cmdText.trim();
    if (c) send(c);
    cmdText = '';
  }

  const fresh = (tag) => pk[tag] && now - pk[tag].at < STALE_MS;
  const f = (v, d = 1) => (v == null ? 'n/a' : Number(v).toFixed(d));
  const time = (t) => new Date(t).toLocaleTimeString('en-GB');

  let st = $derived(pk.STA?.d.st ?? 'NO DATA');
  let pre = $derived(['STANDBY', 'TEST', 'SAFE'].includes(st));
  let test = $derived(st === 'TEST');
  let hkOk = $derived(fresh('HK') && pk.HK.d.ok === 1);
  let linkAge = $derived(lastAt ? Math.max(0, now - lastAt) / 1000 : null);
  let confirmLeft = $derived(Math.max(0, 5 - Math.floor((now - cfrAt) / 1000)));

  // STA "tf" (seconds since SOE) and STS self-test packet
  let tf = $derived(pk.STA?.d.tf);
  function stsText(d) {
    if (d.ok) return 'GO';
    const bad = STS_S.filter((_, i) => !((d.s >> i) & 1)).concat(STS_M.filter((_, i) => !((d.m >> i) & 1)));
    if (d.c >= 0) for (let i = 0; i < 8; i++) {
      if (!((d.c >> i) & 1)) bad.push('cam' + (i + 1));
      else if (d.r >= 0 && !((d.r >> i) & 1)) bad.push('cam' + (i + 1) + ' not rec');
    }
    return 'NO-GO: ' + bad.join(', ') + (d.c < 0 ? ' (cameras not checked)' : '');
  }

  function chamber(i) {
    const p = pk[i < 2 ? 'CH1' : 'CH2']?.d;
    return { p: p?.p[i % 2], T: p?.T[i % 2], ok: fresh(i < 2 ? 'CH1' : 'CH2') };
  }
</script>

{#snippet btn(label, cmd, enabled = true)}
  <button disabled={!enabled} onclick={() => send(cmd)}>{label}</button>
{/snippet}

<header>
  <div class="state">{st}</div>
  {#if tf != null && tf >= 0}<div class="flag on">SOE+{tf} s (T{tf + T_SOE_S >= 0 ? '+' : ''}{tf + T_SOE_S})</div>{/if}
  {#if pk.STS}<div class="ok" class:good={pk.STS.d.ok} class:bad={!pk.STS.d.ok} title="last self-test (SELF_TEST or the final check at SOE), {time(pk.STS.at)}">{stsText(pk.STS.d)}</div>{/if}
  <div class="ok" class:good={hkOk} class:bad={!hkOk}>{hkOk ? 'HK OK' : fresh('HK') ? 'HK FAIL' : 'NO HK'}</div>
  <div class="flag" class:on={pk.STA?.d.soe} {...tip('soe')}>SOE</div>
  <div class="flag" class:on={pk.STA?.d.lo} {...tip('lo')}>LO</div>
  <div class:stale={linkAge == null || linkAge > 2}>link {linkAge == null ? 'n/a' : linkAge.toFixed(1) + ' s'}</div>
  <div {...tip('cmds')}>cmd {pk.ACT?.d.rx ?? 0} / bad {pk.ACT?.d.bad ?? 0}</div>
  <div>bad lines {badLines}</div>

  <div class="conn">
    <select id="mode" bind:value={mode}>
      <option value="demo">Demo</option>
      <option value="ws">Bridge</option>
    </select>
    {#if mode === 'ws'}<input id="url" bind:value={url} size="22" />{/if}
    <button onclick={connect}>Connect</button>
    {#if mode === 'demo' && link}
      <button onclick={() => link.soe()}>Demo SOE</button>
      <button onclick={() => link.lo()}>Demo LO</button>
    {/if}
  </div>
</header>

<main>
  <section>
    <h2>Chambers</h2>
    <div class="grid4">
      {#each [0, 1, 2, 3] as i}
        {@const c = chamber(i)}
        <div class="card" class:stale={!c.ok}>
          <h3>Chamber {i + 1}</h3>
          <div class="big" {...tip(`ch${i}.p`)}>{f(c.p, 1)} <small>hPa</small></div>
          <div {...tip(`ch${i}.T`)}>{f(c.T, 1)} °C</div>
          <div {...tip(`led${i}`)}>LED {pk.ACT?.d.led[i] ?? 'n/a'}</div>
        </div>
      {/each}
    </div>
  </section>

  <section>
    <h2>Cameras <small><span {...tip('campwr')}>power {pk.STA?.d.cam ? 'ON' : 'OFF'}</span>, TV {pk.OBS ? (pk.OBS.d.tv + 1) + (pk.OBS.d.ta ? ' (auto)' : '') : 'n/a'}</small></h2>
    <div class="grid4">
      {#each Array(8) as _, i}
        {@const rt = pk.CAM?.d.rt[i]}
        {@const recOk = ((pk.CAM?.d.rec ?? 0) >> i) & 1}
        <div class="card cam" class:stale={!fresh('CAM')} class:bad={pk.STA?.d.cam && rt === -1}>
          <h3>Cam {i + 1}{#if st === 'ACTIVE' && pk.STA?.d.cam} <small>{recOk ? 'REC ✓' : 'REC ?'}</small>{/if}</h3>
          <div {...tip(`cam${i}`)}>{rt == null || rt < 0 ? (pk.STA?.d.cam ? 'no reply' : 'off') : rt + ' min left'}</div>
        </div>
      {/each}
    </div>
  </section>

  <section>
    <h2>Mainboard</h2>
    <table>
      <tbody>
        <tr class:stale={!fresh('BAR')}><td>Barometer</td><td><span {...tip('bar.p')}>{f(pk.BAR?.d.p)} hPa</span>, <span {...tip('bar.T')}>{f(pk.BAR?.d.T)} °C</span></td></tr>
        <tr class:stale={!fresh('ACC')}><td>Accel [g]</td><td><span {...tip('acc')}>{(pk.ACC?.d.a ?? []).map((v) => f(v, 2)).join(' / ') || 'n/a'}</span></td></tr>
        <tr class:stale={!fresh('GYR')}><td>Gyro [dps]</td><td><span {...tip('gyr')}>{(pk.GYR?.d.g ?? []).map((v) => f(v, 1)).join(' / ') || 'n/a'}</span></td></tr>
        <tr class:stale={!fresh('HK')}><td>5 V rail</td><td><span {...tip('v')}>{f(pk.HK?.d.v, 2)} V</span></td></tr>
        <tr class:stale={!fresh('HK')}><td>Sensors</td><td><span {...tip('ns')}>{pk.HK?.d.ns ?? 'n/a'} / 6</span></td></tr>
        <tr class:stale={!fresh('HK')}><td>SD card</td><td>{pk.HK ? (pk.HK.d.sd ? 'OK' : 'FAILED') : 'n/a'}</td></tr>
        <tr><td>Boot folder</td><td>{pk.MEM ? String(pk.MEM.d.bt).padStart(4, '0') : 'n/a'} <small>(GET_MEMORY_STATUS)</small></td></tr>
        <tr><td>Flash free</td><td>{pk.MEM ? pk.MEM.d.fl + ' KB' + (pk.MEM.d.er ? ', erasing' : '') : 'n/a'}</td></tr>
      </tbody>
    </table>
  </section>

  <section>
    <h2>Commands</h2>
    <div class="row">
      {@render btn('Ping', 'PING')}
      {@render btn('Status', 'GET_STATUS')}
      {@render btn('HK', 'GET_HK')}
      {@render btn('Obs', 'GET_OBS_STATUS')}
      {@render btn('Memory', 'GET_MEMORY_STATUS')}
      {@render btn('Self-test', 'SELF_TEST')}
      {@render btn('Save memory', 'SAVE_MEMORY')}
    </div>
    <div class="row">
      {@render btn('Enter test', 'ENTER_TEST_MODE', st === 'STANDBY')}
      {@render btn('Exit test', 'EXIT_TEST_MODE', test)}
      {@render btn('Enter safe', 'ENTER_SAFE_MODE', st === 'STANDBY' || test)}
      {@render btn('Exit safe', 'EXIT_SAFE_MODE', st === 'SAFE')}
      {@render btn('Re-arm', 'REARM', st === 'SHUTDOWN')}
      {#if now - rebootAskAt < 5000}
        <button class="danger" onclick={() => { rebootAskAt = 0; send('REBOOT'); }}>Confirm reboot</button>
      {:else}
        <button disabled={st === 'ACTIVE'} onclick={() => (rebootAskAt = Date.now())}>Reboot</button>
      {/if}
    </div>
    <div class="row protected">
      {@render btn('Clear flash (1/2)', 'CLEAR_MEMORY', pre)}
      {@render btn(`Confirm clear (2/2)${confirmLeft ? ' ' + confirmLeft + ' s' : ''}`, 'CLEAR_MEMORY CONFIRM', pre && confirmLeft > 0)}
    </div>

    <h3>Test mode only</h3>
    <div class="row">
      {@render btn('Cameras on', 'CAM_ON', test)}
      {@render btn('Cameras off', 'CAM_OFF', test)}
      {@render btn('LEDs off', 'LED_OFF', test)}
    </div>
    <div class="row">
      LED level <input id="led-level" type="range" min="0" max="255" bind:value={ledLevel} /> {ledLevel}
      {@render btn('All', `LED_ALL ${ledLevel}`, test)}
      {#each [1, 2, 3, 4] as k}{@render btn(`Ch ${k}`, `LED ${k} ${ledLevel}`, test)}{/each}
    </div>
    <div class="row">
      TV
      {#each Array(8) as _, n}{@render btn(`${n + 1}`, `TV ${n}`, test || st === 'ACTIVE')}{/each}
      {@render btn('Auto', 'TV 8', test || st === 'ACTIVE')}
    </div>

    <form class="row" onsubmit={(e) => { e.preventDefault(); sendText(); }}>
      <input id="raw-cmd" bind:value={cmdText} placeholder="raw command, e.g. LED 2 128" size="30" />
      <button type="submit">Send</button>
    </form>
  </section>

  <section class="log">
    <h2>Events</h2>
    {#each events as e}
      <div class="ev {e.t}"><span>{time(e.at)}</span> <b>{e.t}</b> {e.text}</div>
    {/each}
  </section>
</main>

<svelte:window
  onpointerdown={(e) => hover && !e.target.closest?.('[data-tip]') && hide()}
  onscroll={hide}
/>

{#if hover}
  {#key hover.key}
    {@const m = META[hover.key]}
    <div class="pop" role="tooltip" style="left: {hover.left}px; top: {hover.top}px" transition:fade={{ duration: 150 }}>
      <Chart title={m[0]} unit={m[1]} d={m[2]} names={m[3]} pts={hist[hover.key] ?? []} {now} />
    </div>
  {/key}
{/if}

<style>
  /* Console look: square edges, 1px rules, mono numbers, no shadows. Panels in an auto-fit grid, one column on phones. */
  :global(:root) {
    --bg: #e6e8ea; --panel: #f6f7f7; --head: #dcdfe2; --line: #b4bbc1; --line-soft: #d4d8dc;
    --fg: #14181b; --muted: #535c64; --faint: #868e95; --btn: #eceef0;
    --accent: #1d5a8c; --good: #2b6e3f; --bad: #a8231b; --warn: #855700; --bad-soft: #f1dcda;
    --font: "IBM Plex Sans", system-ui, -apple-system, "Segoe UI", sans-serif;
    --mono: "IBM Plex Mono", ui-monospace, "SF Mono", Menlo, Consolas, monospace;
    color-scheme: light;  /* light only, also when the OS or viewer is in dark mode */
  }
  :global(body) { margin: 0; font: 14px/1.4 var(--font); background: var(--bg); color: var(--fg); font-variant-numeric: tabular-nums; }

  header {
    display: flex; flex-wrap: wrap; align-items: stretch; gap: 0;
    background: var(--panel); border-bottom: 1px solid var(--line);
    position: sticky; top: env(safe-area-inset-top, 0px); z-index: 1;
    font-family: var(--mono); font-size: 13px;
  }
  header > * { display: flex; align-items: center; padding: 8px 12px; border-right: 1px solid var(--line-soft); }
  .state { font-weight: 600; font-size: 16px; letter-spacing: 0.06em; min-width: 90px; }
  .ok { font-weight: 600; color: #fff; letter-spacing: 0.04em; }
  .ok.good { background: var(--good); }
  .ok.bad { background: var(--bad); }
  .flag { color: var(--faint); font-weight: 600; letter-spacing: 0.06em; }
  .flag.on { background: var(--accent); color: var(--panel); }
  .conn { margin-left: auto; gap: 6px; flex-wrap: wrap; border-right: 0; border-left: 1px solid var(--line-soft); font-family: var(--font); }

  main { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(420px, 100%), 1fr)); gap: 12px; padding: 12px 16px; align-items: start; }
  section { background: var(--panel); border: 1px solid var(--line); padding: 0 12px 12px; min-width: 0; }
  h2 {
    margin: 0 -12px 12px; padding: 6px 12px; background: var(--head); border-bottom: 1px solid var(--line);
    font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.08em;
  }
  h2 small { color: var(--muted); font-weight: 400; font-family: var(--mono); text-transform: none; letter-spacing: 0; margin-left: 8px; }
  h3 { margin: 0 0 4px; font-size: 11px; font-weight: 600; color: var(--muted); text-transform: uppercase; letter-spacing: 0.08em; }

  /* cells share 1px rules instead of floating cards */
  .grid4 { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 1px; background: var(--line); border: 1px solid var(--line); }
  @media (max-width: 480px) { .grid4 { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
  .card { background: var(--panel); padding: 8px; font-family: var(--mono); font-size: 13px; }
  .card h3 { font-family: var(--font); }
  .card.bad { background: var(--bad-soft); color: var(--bad); }
  .big { font-size: 16px; font-weight: 500; white-space: nowrap; }
  .big small { font-size: 11px; }
  small { color: var(--muted); }
  .stale { opacity: 0.4; }

  table { width: 100%; border-collapse: collapse; }
  td { padding: 4px 0; border-bottom: 1px solid var(--line-soft); }
  td:first-child { color: var(--muted); width: 40%; }
  td:last-child { font-family: var(--mono); font-size: 13px; }
  td small { font-family: var(--font); }

  .row { display: flex; flex-wrap: wrap; gap: 4px; align-items: center; margin-bottom: 8px; }
  input:not([type="range"]), select { font: inherit; padding: 4px 6px; border: 1px solid var(--line); border-radius: 0; background: var(--panel); color: var(--fg); max-width: 100%; }
  #raw-cmd { font-family: var(--mono); font-size: 13px; }
  input[type="range"] { accent-color: var(--accent); }
  button { font: inherit; font-size: 13px; padding: 4px 10px; border: 1px solid var(--line); border-radius: 0; background: var(--btn); color: var(--fg); cursor: pointer; }
  button:hover:not(:disabled) { background: var(--head); }
  button:focus-visible, input:focus-visible, select:focus-visible { outline: 1px solid var(--accent); outline-offset: 1px; }
  button:disabled { opacity: 0.35; cursor: default; }
  .protected button, button.danger { border-color: var(--bad); color: var(--bad); }

  .log { max-height: 420px; overflow-y: auto; font-family: var(--mono); font-size: 12px; }
  .ev span { color: var(--faint); }
  .ev.ERR { color: var(--bad); }
  .ev.CFR { color: var(--warn); }
  .ev.TX { color: var(--muted); }

  :global([data-tip]) { cursor: crosshair; }
  :global([data-tip]:hover) { background: var(--head); }
  :global([data-tip]:focus-visible) { outline: 1px solid var(--accent); outline-offset: 1px; }
  .pop {
    position: fixed; z-index: 10; width: min(320px, calc(100vw - 16px)); box-sizing: border-box; pointer-events: none;
    background: var(--panel); border: 1px solid var(--line); border-top: 2px solid var(--accent); padding: 8px 10px;
  }
</style>
