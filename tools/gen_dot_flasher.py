#!/usr/bin/env python3
"""Build a single self-contained HTML flasher for an ARGUS-Design-OS build.

Usage: gen_dot_flasher.py <pio build dir> <boot_app0.bin> <commit> <esp-web-tools bundle.js> <out.html>

Inlines ESP Web Tools (flasher/vendor/esp-web-tools 10.4.0, bundled into one
ES module with esbuild by CI) and the
four component binaries as base64. At runtime the page turns them into blob:
URLs and hands ESP Web Tools a blob: manifest, so nothing is fetched from the
network. Open the file locally in Chrome or Edge (file:// is a secure context,
so Web Serial is available)."""
import base64, hashlib, html, pathlib, sys

build_dir, boot_app0, commit, bundle_path, out = sys.argv[1:6]
bundle = pathlib.Path(bundle_path).read_text()

parts = [
    ("bootloader.bin", pathlib.Path(build_dir) / "bootloader.bin", 0x0),
    ("partitions.bin", pathlib.Path(build_dir) / "partitions.bin", 0x8000),
    ("boot_app0.bin",  pathlib.Path(boot_app0),                    0xE000),
    ("firmware.bin",   pathlib.Path(build_dir) / "firmware.bin",   0x10000),
]
rows, blobs = [], []
for name, path, off in parts:
    data = path.read_bytes()
    sha = hashlib.sha256(data).hexdigest()
    rows.append(f'<tr><td><code>0x{off:X}</code></td><td>{name}</td>'
                f'<td class="num">{len(data):,}</td><td><code title="{sha}">{sha[:12]}</code></td></tr>')
    blobs.append(f'{{name:"{name}",offset:{off},b64:"{base64.b64encode(data).decode()}"}}')

short = commit[:7]
page = f"""<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ARGUS Dot Flasher</title>
<style>
:root {{
  color-scheme: dark;
  --ground: #0A0A0A;
  --panel: #121212;
  --line: #262626;
  --ink: #F2F2F2;
  --dim: #8A8A8A;
  --idle: #5C5C5C;
  --red: #E02020;
  --mono: ui-monospace, "SFMono-Regular", "Cascadia Mono", Menlo, Consolas, monospace;
  --sans: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
}}
* {{ box-sizing: border-box; }}
body {{ margin: 0; background: var(--ground); color: var(--ink); font: 15px/1.55 var(--sans); }}
main {{ max-width: 680px; margin: 0 auto; padding-inline: 20px; padding-block: 40px 56px; display: grid; gap: 28px; }}
.dots {{ display: flex; gap: 10px; }}
.dots i {{ width: 6px; height: 6px; border-radius: 50%; background: var(--idle); animation: w 1.2s ease-in-out infinite; }}
.dots i:nth-child(2) {{ animation-delay: .08s }} .dots i:nth-child(3) {{ animation-delay: .16s }}
.dots i:nth-child(4) {{ animation-delay: .24s }} .dots i:nth-child(5) {{ animation-delay: .32s }}
.dots i:nth-child(6) {{ animation-delay: .40s }} .dots i:nth-child(7) {{ animation-delay: .48s }}
.dots i:nth-child(8) {{ animation-delay: .56s }}
@keyframes w {{ 0%,100% {{ background: var(--ink) }} 50% {{ background: var(--red) }} }}
@media (prefers-reduced-motion: reduce) {{ .dots i {{ animation: none; background: var(--ink); }} }}
h1 {{ margin: 0; font: 600 30px/1.15 var(--mono); letter-spacing: .02em; text-wrap: balance; }}
.sub {{ margin: 6px 0 0; color: var(--dim); font-family: var(--mono); font-size: 13px; }}
.install {{ background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 22px; display: grid; gap: 14px; }}
esp-web-install-button button[slot="activate"] {{
  font: 600 15px var(--mono); letter-spacing: .06em; text-transform: uppercase;
  background: var(--red); color: #fff; border: 0; border-radius: 6px; padding: 14px 22px; cursor: pointer;
}}
esp-web-install-button button[slot="activate"]:focus-visible {{ outline: 2px solid var(--ink); outline-offset: 3px; }}
.note {{ color: var(--dim); font-size: 13px; margin: 0; }}
.warn {{ color: var(--ink); border-left: 2px solid var(--red); padding-left: 12px; margin: 0; font-size: 14px; }}
h2 {{ margin: 0 0 8px; font: 600 12px var(--mono); letter-spacing: .14em; text-transform: uppercase; color: var(--dim); }}
ol {{ margin: 0; padding-left: 20px; display: grid; gap: 6px; }}
.tbl {{ overflow-x: auto; }}
table {{ width: 100%; border-collapse: collapse; font-size: 13px; }}
td, th {{ text-align: left; padding: 7px 10px 7px 0; border-bottom: 1px solid var(--line); white-space: nowrap; }}
th {{ color: var(--dim); font-weight: 500; font-family: var(--mono); font-size: 11px; letter-spacing: .1em; text-transform: uppercase; }}
.num {{ font-variant-numeric: tabular-nums; text-align: right; }}
code {{ font-family: var(--mono); font-size: 12.5px; }}
#status {{ font-family: var(--mono); font-size: 13px; color: var(--dim); margin: 0; }}
</style>
</head>
<body>
<main>
  <header>
    <div class="dots" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i><i></i><i></i><i></i></div>
    <h1 style="margin-top:18px">ARGUS Dot Flasher</h1>
    <p class="sub">ARGUS-Design-OS &middot; feat/dot-watchface &middot; {html.escape(short)} &middot; LILYGO T-Watch Ultra (ESP32-S3)</p>
  </header>

  <section class="install" aria-labelledby="h-install">
    <h2 id="h-install">Installer</h2>
    <esp-web-install-button id="installer">
      <button slot="activate">Installer sur la montre</button>
      <span slot="unsupported">Ce navigateur ne g&egrave;re pas le port s&eacute;rie. Ouvre ce fichier dans Chrome ou Edge sur ordinateur.</span>
      <span slot="not-allowed">Ouvre ce fichier directement depuis ton disque (double-clic) dans Chrome ou Edge.</span>
    </esp-web-install-button>
    <p id="status">Pr&eacute;paration des binaires&hellip;</p>
    <p class="warn">Ferme d'abord tout ce qui utilise le port de la montre : moniteur s&eacute;rie, PlatformIO, Arduino IDE, un autre onglet de flash.</p>
  </section>

  <section aria-labelledby="h-steps">
    <h2 id="h-steps">&Eacute;tapes</h2>
    <ol>
      <li>Branche la montre en USB, puis clique <strong>Installer sur la montre</strong>.</li>
      <li>Choisis le port de la montre dans la fen&ecirc;tre du navigateur.</li>
      <li>Si on te propose d'effacer la montre, r&eacute;ponds non : tu gardes la m&eacute;moire interne (mode ARGUS, notifications, d&eacute;tecteurs m&eacute;moris&eacute;s). La carte SD n'est jamais touch&eacute;e.</li>
      <li>&Agrave; la fin, red&eacute;marre la montre, puis <strong>Settings &gt; Watch Face &gt; Dot</strong>.</li>
    </ol>
    <p class="note" style="margin-top:10px">Si la connexion &eacute;choue : d&eacute;branche la montre, maintiens <strong>BOOT</strong> en la rebranchant (ou BOOT + RESET), puis recommence.</p>
  </section>

  <section aria-labelledby="h-parts">
    <h2 id="h-parts">Contenu flash&eacute;</h2>
    <div class="tbl"><table>
      <thead><tr><th>Adresse</th><th>Fichier</th><th class="num">Octets</th><th>SHA-256</th></tr></thead>
      <tbody>{''.join(rows)}</tbody>
    </table></div>
    <p class="note" style="margin-top:10px">Quatre fichiers s&eacute;par&eacute;s &agrave; leurs adresses, en-t&ecirc;tes flash conserv&eacute;s (jamais d'image fusionn&eacute;e en QIO). Tout est embarqu&eacute; dans ce fichier : aucune connexion r&eacute;seau n&eacute;cessaire.</p>
  </section>
</main>

<script>
(function () {{
  const PARTS = [{",".join(blobs)}];
  const status = document.getElementById('status');
  try {{
    const parts = PARTS.map(p => {{
      const raw = atob(p.b64), bytes = new Uint8Array(raw.length);
      for (let i = 0; i < raw.length; i++) bytes[i] = raw.charCodeAt(i);
      return {{ path: URL.createObjectURL(new Blob([bytes], {{ type: 'application/octet-stream' }})), offset: p.offset }};
    }});
    const manifest = {{
      name: 'ARGUS-Design-OS (Dot)',
      version: '{html.escape(short)}',
      new_install_prompt_erase: true,
      builds: [{{ chipFamily: 'ESP32-S3', parts }}]
    }};
    const url = URL.createObjectURL(new Blob([JSON.stringify(manifest)], {{ type: 'application/json' }}));
    document.getElementById('installer').setAttribute('manifest', url);
    status.textContent = 'Prêt : 4 fichiers chargés, build {html.escape(short)}.';
  }} catch (e) {{
    status.textContent = 'Erreur de préparation : ' + e.message;
  }}
}})();
</script>
<script type="module">
{bundle}
</script>
</body>
</html>
"""
pathlib.Path(out).write_text(page)
print(f"wrote {out} ({len(page)/1e6:.2f} MB)")
