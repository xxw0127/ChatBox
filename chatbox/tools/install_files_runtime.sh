#!/usr/bin/env bash
# Idempotent runtime setup for ChatBox file transfer.
# Usage: sudo ./install_files_runtime.sh [/home/ubuntu/chatbox/files]
set -euo pipefail

FILES_DIR="${1:-/home/ubuntu/chatbox/files}"
SERVICE_USER="${SERVICE_USER:-ubuntu}"

if ! command -v ffmpeg >/dev/null 2>&1 || ! command -v ffprobe >/dev/null 2>&1; then
    echo "installing ffmpeg (needed for image/GIF thumbnails)"
    apt-get update -y
    DEBIAN_FRONTEND=noninteractive apt-get install -y ffmpeg
fi

install -d -m 0700 "${FILES_DIR}"
install -d -m 0700 "${FILES_DIR}/tmp"
install -d -m 0700 "${FILES_DIR}/thumbs"
chown -R "${SERVICE_USER}:${SERVICE_USER}" "${FILES_DIR}"

echo "files runtime ready: ${FILES_DIR}"
ffmpeg -version | head -n 1
ffprobe -version | head -n 1
