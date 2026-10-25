<script>
  // Last-minute line chart for the hover popup. pts: [{ t, v: [..] }], one array entry per series.
  let { title, pts, now, unit = '', d = 1, names = null } = $props();

  const WIN = 60000, W = 300, H = 100;
  const COLORS = ['var(--accent)', 'var(--good)', 'var(--warn)'];

  let win = $derived(pts.filter((p) => p.t >= now - WIN));
  let n = $derived(win.at(-1)?.v.length ?? 1);
  let range = $derived.by(() => {
    let lo = Infinity, hi = -Infinity;
    for (const p of win) for (const v of p.v) if (v != null) { lo = Math.min(lo, v); hi = Math.max(hi, v); }
    if (lo === Infinity) return null;
    if (hi - lo < 1e-9) { const pad = Math.abs(hi) * 0.001 || 0.5; lo -= pad; hi += pad; }
    const pad = (hi - lo) * 0.08;
    return [lo - pad, hi + pad];
  });

  const x = (t) => (W * (1 - (now - t) / WIN)).toFixed(1);
  const y = (v) => (H - ((v - range[0]) / (range[1] - range[0])) * H).toFixed(1);
  function path(i) {
    let s = '', pen = false;
    for (const p of win) {
      const v = p.v[i];
      if (v == null) { pen = false; continue; }  // gap where the value was missing
      s += (pen ? 'L' : 'M') + x(p.t) + ' ' + y(v);
      pen = true;
    }
    return s;
  }
  const fmt = (v) => (v == null ? 'n/a' : v.toFixed(d));
</script>

<div class="head">
  <b>{title}</b>
  <span>
    {#each Array(n) as _, i}
      <span style="color: {n > 1 ? COLORS[i] : 'inherit'}">{names ? names[i] + ' ' : ''}{fmt(win.at(-1)?.v[i])}</span>
    {/each}
    {unit}
  </span>
</div>

{#if range}
  <div class="plot">
    <div class="ylab"><span>{range[1].toFixed(d)}</span><span>{range[0].toFixed(d)}</span></div>
    <svg viewBox="0 0 {W} {H}" preserveAspectRatio="none">
      <line x1="0" y1="0.5" x2={W} y2="0.5" />
      <line x1="0" y1={H / 2} x2={W} y2={H / 2} />
            {#each Array(n) as _, i}
        <path d={path(i)} stroke={COLORS[i]} />
      {/each}
    </svg>
    <div></div>
    <div class="xlab"><span>-60 s</span><span>-30 s</span><span>now</span></div>
  </div>
{:else}
  <div class="empty">no data in the last minute</div>
{/if}

<style>
  .head b { font-weight: 600; text-transform: uppercase; letter-spacing: 0.06em; font-size: 11px; }
  .head { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; margin-bottom: 6px; font-size: 12px; }
  .head > span { display: flex; gap: 8px; color: var(--muted); font-family: var(--mono); }
  .plot { display: grid; grid-template-columns: auto minmax(0, 1fr); gap: 2px 6px; }
  .ylab { display: flex; flex-direction: column; justify-content: space-between; font-size: 10px; color: var(--faint); text-align: right; font-family: var(--mono); }
  svg { width: 100%; height: 100px; display: block; border-left: 1px solid var(--line); border-bottom: 1px solid var(--line); }
  line { stroke: var(--line-soft); stroke-width: 1; vector-effect: non-scaling-stroke; }
  path { fill: none; stroke-width: 1.25; vector-effect: non-scaling-stroke; stroke-linejoin: miter; }
  .xlab { display: flex; justify-content: space-between; font-size: 10px; color: var(--faint); font-family: var(--mono); }
  .empty { padding: 30px 0; text-align: center; color: var(--faint); font-size: 12px; }
</style>
