#!/usr/bin/env bash
set -euo pipefail

# SSH: bind mount is read-only, so copy to ~/.ssh and fix permissions
mkdir -p /home/gandalf/.ssh
cp -r /tmp/host-ssh/. /home/gandalf/.ssh/
chmod 700 /home/gandalf/.ssh
chmod 600 /home/gandalf/.ssh/* 2>/dev/null || true

# Claude profile volume can come up owned by root on first attach
sudo chown -R gandalf:gandalf /home/gandalf/.claude 2>/dev/null || true
