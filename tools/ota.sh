#!/usr/bin/env bash
# Firmware drahtlos auf die Kletterwand laden.
#
#   tools/ota.sh                  # an kilterboard.local (aus secrets.ini)
#   tools/ota.sh 192.168.4.1      # an eine feste Adresse
#
# Die Wand muss dafuer schon einmal per USB geflasht worden sein und im
# selben Netz haengen (oder man verbindet sich mit ihrem eigenen WLAN).
set -euo pipefail
cd "$(dirname "$0")/.."

if [ ! -f secrets.ini ]; then
  echo "secrets.ini fehlt -- secrets.example.ini kopieren und ausfuellen." >&2
  exit 1
fi

echo "Baue Firmware ..."
pio run -e kilterboard_ota

if [ $# -gt 0 ]; then
  echo "Lade hoch an $1 ..."
  pio run -e kilterboard_ota -t upload --upload-port "$1"
else
  pio run -e kilterboard_ota -t upload
fi
