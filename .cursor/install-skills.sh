#!/usr/bin/env bash
set -euo pipefail

REPO_SLUG="${CURSOR_SKILLS_REPO:-tmyk-io/cursor-skills}"
CLONE_URL="${CURSOR_SKILLS_CLONE_URL:-https://github.com/${REPO_SLUG}.git}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "Cloning ${REPO_SLUG} (depth 1)..."
git clone --depth 1 "$CLONE_URL" "$TMP_DIR/cursor-skills"
bash "$TMP_DIR/cursor-skills/scripts/cloud-install.sh"
