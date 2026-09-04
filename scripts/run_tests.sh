#!/usr/bin/env bash
# Full suite. Run inside the container: ./scripts/dev.sh test
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
echo "=== parser unit tests ==="        ; ./build/parserTest            || fail=1
echo; echo "=== concurrency smoke ==="  ; python3 scripts/smoke_test.py > /tmp/o 2>&1 && tail -3 /tmp/o || { tail -20 /tmp/o; fail=1; }
echo; echo "=== framing e2e ==="        ; python3 scripts/e2e_test.py   > /tmp/o 2>&1 && tail -3 /tmp/o || { tail -20 /tmp/o; fail=1; }
echo; echo "=== backpressure ==="       ; python3 scripts/backpressure_test.py > /tmp/o 2>&1 && tail -4 /tmp/o || { tail -20 /tmp/o; fail=1; }
echo; echo "=== realtime streaming ===" ; python3 scripts/stream_test.py > /tmp/o 2>&1 && tail -6 /tmp/o || { tail -20 /tmp/o; fail=1; }
echo
[ $fail -eq 0 ] && echo "ALL SUITES PASSED" || echo "SOME SUITES FAILED"
exit $fail
