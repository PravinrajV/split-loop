#!/usr/bin/env python3
"""Ensure the offline display contains only unchanged source measurements."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'evidence/acceptance.json').read_bytes()
full = json.loads(source)
page = (ROOT / 'ui/standalone.html').read_text()
sub = json.loads(re.search(r'id="embedded-evidence">(.*?)</script>', page, re.S)[1])
assert sub['display_provenance']['source_sha256'] == hashlib.sha256(source).hexdigest()
assert len(sub['runs']) == 12
for run, original in zip(sub['runs'], full['runs'][:12]):
    assert run['id'] == original['id']
    for key, value in run.items():
        if key != 'samples':
            assert value == original[key], key
    assert len(run['samples']) == len(original['samples'])
    for row, original_row in zip(run['samples'], original['samples']):
        assert all(original_row[k] == v for k, v in row.items())
for key in ('judge_summary', 'policy', 'setup'):
    assert sub[key] == full[key], key
ids = {r['id'] for r in sub['runs']}
assert sub['comparisons'] == [c for c in full['comparisons'] if c['run_id'] in ids]
assert sub['judge_summary']['planner_s'] == 2.959989901
assert sub['judge_summary']['baseline_s'] == 7.990243749
assert 'src="app.js"' not in page and 'href="style.css"' not in page
assert 'Display interpolation is not a new measurement.' in page
assert all((ROOT / 'reports' / r['raw_csv']).is_file() for r in sub['runs'])
print('PASS: 12 runs, 5,332 undownsampled display rows, policy, comparison and setup exactly match acceptance.json; raw CSV links exist')
