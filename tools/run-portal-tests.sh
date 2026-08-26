#!/bin/sh
# Extract the portal's script and run every regression harness in tools/.
# Run this after ANY edit to ds5-config-portal.html.
set -e
cd "$(dirname "$0")/.."
python3 - << 'PY'
import re
s = open('ds5-config-portal.html', encoding='utf-8').read()
b = re.findall(r"<script[^>]*>(.*?)</script>", s, re.S)
open('/tmp/portal.js','w',encoding='utf-8').write("\n".join(b))
PY
node --check /tmp/portal.js && echo "SYNTAX OK"
node tools/portal-coverage-test.js
node tools/portal-render-test.js
node tools/portal-attr-test.js
node tools/portal-align-test.js
node tools/portal-validate-test.js
node tools/portal-dedup-test.js
node tools/portal-reader-test.js
node tools/portal-force-test.js
node tools/portal-macro-test.js
node tools/portal-search-test.js
node tools/portal-motion-test.js
node tools/portal-buttons-test.js
node tools/portal-viz-test.js
[ -f /mnt/user-data/uploads/R2_3x.json ] && node tools/portal-fileload-test.js || true
