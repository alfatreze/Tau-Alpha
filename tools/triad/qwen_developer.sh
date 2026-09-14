#!/bin/sh
set -eu

# Use Claude Code as a no-tools text transport to the local Qwen model through
# UniClaudeProxy. The Director disables built-in tools and MCP for Qwen calls.
export ANTHROPIC_AUTH_TOKEN="local-lm-studio"
export ANTHROPIC_BASE_URL="http://127.0.0.1:9223"
export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC="1"
export CLAUDE_CODE_MAX_OUTPUT_TOKENS="4096"
export API_TIMEOUT_MS="300000"
export CLAUDE_BASH_NO_LOGIN="1"

exec /usr/local/bin/claude "$@"
