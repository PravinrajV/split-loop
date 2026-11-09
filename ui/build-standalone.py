#!/usr/bin/env python3
"""Embed a display-only subset of immutable measured evidence; no resimulation."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'evidence/acceptance.json'
# Default recipe covers all six storyboard beats, plus malformed/overload.
# Keep every raw sample in those runs, but only the fields consumed by the view.
FIELDS = ('tick', 'mono_ns', 'x', 'y', 'sensor_x', 'sensor_y',
          'requested_x', 'requested_y', 'reason', 'advice_age_ns')

def build():
    original = SOURCE.read_bytes()
    full = json.loads(original)
    runs = full['runs'][:12]
    ids = {r['id'] for r in runs}
    embedded = {k: full[k] for k in ('disclaimer', 'generated_at_est', 'source_log',
                                    'setup', 'policy', 'matrix_pass', 'ai_label', 'judge_summary')}
    embedded['runs'] = [{**r, 'samples': [{k: s[k] for k in FIELDS} for s in r['samples']]}
                        for r in runs]
    embedded['comparisons'] = [c for c in full['comparisons'] if c['run_id'] in ids]
    embedded['display_provenance'] = {
        'source': '../evidence/acceptance.json',
        'source_sha256': hashlib.sha256(original).hexdigest(),
        'scope': 'Default-recipe runs r00–r11 only; unused sample fields omitted. No samples downsampled.',
        'display_only': True,
        'note': 'Display interpolation is not a new measurement. Raw CSVs/reports remain the source of truth.'
    }
    # Escape less-than so evidence can never terminate its inert JSON script element.
    data = json.dumps(embedded, separators=(',', ':'), ensure_ascii=False).replace('<', '\\u003c')
    page = (ROOT / 'ui/index.html').read_text()
    page = page.replace('<link rel="stylesheet" href="style.css">',
                        '<style>' + (ROOT / 'ui/style.css').read_text() + '</style>')
    page = page.replace('<script src="app.js"></script>',
                        '<script type="application/json" id="embedded-evidence">' + data + '</script>\n<script>'
                        + (ROOT / 'ui/app.js').read_text() + '</script>')
    page = page.replace('<title>Split Loop — simulated cassette transfer</title>',
                        '<title>Split Loop — offline measured QNX replay</title>')
    destination = ROOT / 'ui/standalone.html'
    destination.write_text(page)
    print(f'{destination}: {destination.stat().st_size:,} bytes; {len(runs)} runs; '
          f'{sum(len(r["samples"]) for r in runs):,} complete display sample rows')
    return embedded

if __name__ == '__main__':
    build()
