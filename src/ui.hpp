#pragma once
// Single-page search UI served at "/".
inline const char* kIndexHtml = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>searchx</title>
<style>
  :root { --bg:#fff; --fg:#1f2328; --muted:#656d76; --line:#d0d7de; --accent:#0969da; }
  @media (prefers-color-scheme: dark) { :root { --bg:#0d1117; --fg:#e6edf3; --muted:#8b949e; --line:#30363d; --accent:#58a6ff; } }
  body { margin:0; background:var(--bg); color:var(--fg); font:16px/1.5 system-ui, sans-serif; }
  main { max-width:720px; margin:0 auto; padding:48px 20px; }
  h1 { font-size:28px; margin:0 0 4px; } h1 span { color:var(--accent); }
  p.sub { margin:0 0 24px; color:var(--muted); }
  input { width:100%; box-sizing:border-box; padding:12px 14px; font-size:17px; color:var(--fg);
          background:transparent; border:1px solid var(--line); border-radius:8px; }
  input:focus { outline:2px solid var(--accent); border-color:transparent; }
  #meta { margin:14px 0; color:var(--muted); font-size:14px; min-height:20px; }
  .hit { padding:12px 0; border-top:1px solid var(--line); }
  .hit b { color:var(--accent); } .hit small { color:var(--muted); } .hit p { margin:4px 0 0; color:var(--muted); font-size:15px; }
  code { background:var(--line); padding:1px 5px; border-radius:4px; font-size:13px; }
</style></head><body><main>
<h1><span>search</span>x</h1>
<p class="sub">Inverted index &middot; BM25 &middot; try <code>quick brown</code>, <code>fox OR dog</code>, <code>"lazy dog"</code>, <code>quick -fox</code></p>
<input id="q" placeholder="Search..." autofocus autocomplete="off">
<div id="meta"></div><div id="out"></div>
<script>
const q = document.getElementById('q'), out = document.getElementById('out'), meta = document.getElementById('meta');
let timer, seq = 0;
async function run() {
  const text = q.value.trim(), mine = ++seq;
  if (!text) { out.replaceChildren(); meta.textContent = ''; return; }
  try {
    const r = await (await fetch('/search?k=20&q=' + encodeURIComponent(text))).json();
    if (mine !== seq) return;
    meta.textContent = r.total.toLocaleString() + ' results in ' + r.tookMs.toFixed(2) + ' ms' + (r.cached ? ' (cached)' : '');
    out.replaceChildren(...r.results.map(h => {
      const d = document.createElement('div'); d.className = 'hit';
      const t = document.createElement('b'); t.textContent = h.title;
      const s = document.createElement('small'); s.textContent = '  #' + h.id + ' \u00b7 score ' + h.score.toFixed(2);
      const p = document.createElement('p'); p.textContent = h.snippet;
      d.append(t, s, p); return d;
    }));
  } catch (e) { meta.textContent = 'Error: ' + e.message; }
}
q.addEventListener('input', () => { clearTimeout(timer); timer = setTimeout(run, 120); });
</script></main></body></html>)HTML";
