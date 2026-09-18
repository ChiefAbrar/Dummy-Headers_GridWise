#!/usr/bin/env bash
set -euo pipefail
BASE_URL="${1:-http://127.0.0.1:8080}"
command -v curl >/dev/null || { echo 'curl is required'; exit 2; }

echo 'Health:'
curl -fsS "$BASE_URL/health"
echo

echo 'Optimization request:'
curl -fsS \
  -X POST "$BASE_URL/optimize-energy" \
  -H 'Content-Type: application/json' \
  --data-binary @tests/sample_request.json
printf '\n'
