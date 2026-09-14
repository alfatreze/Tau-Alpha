#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
LMS=/Users/abel.santos/.lmstudio/bin/lms
CPTR="$ROOT/.venv-cptr/bin/cptr"
PYTHON="$ROOT/.venv-cptr/bin/python"

if ! curl -fsS --max-time 3 http://127.0.0.1:1234/v1/models >/dev/null; then
    "$LMS" server start --port 1234
    attempt=0
    until curl -fsS --max-time 3 http://127.0.0.1:1234/v1/models >/dev/null; do
        attempt=$((attempt + 1))
        if [ "$attempt" -ge 10 ]; then
            echo "LM Studio did not become ready on port 1234" >&2
            exit 1
        fi
        sleep 1
    done
fi

if ! curl -fsS --max-time 3 http://127.0.0.1:1234/v1/models | grep -q 'qwen2.5-coder-7b-instruct'; then
    "$LMS" load qwen2.5-coder-7b-instruct \
        --identifier qwen2.5-coder-7b-instruct \
        --context-length 16000 \
        --gpu auto
fi

cd "$ROOT/UniClaudeProxy"
"$PYTHON" -m uvicorn app.main:app --host 127.0.0.1 --port 9223 &
PROXY_PID=$!
trap 'kill "$PROXY_PID" 2>/dev/null || true' EXIT INT TERM

cd "$ROOT"
CPTR_DATA_DIR="$ROOT/.cptr-data" "$CPTR" run \
    --host 127.0.0.1 --port 8000
