#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if ! command -v java >/dev/null 2>&1; then
  osascript -e 'display dialog "需要先安装 Java Runtime 才能运行汉化器。" buttons {"OK"} default button "OK"'
  exit 1
fi

GAME_DIR="$HOME/Library/Application Support/FINAL FANTASY XIV ONLINE/Bottles/published_Final_Fantasy/drive_c/Program Files (x86)/SquareEnix/FINAL FANTASY XIV - A Realm Reborn"
if [ -d "$GAME_DIR" ] && [ -f "$GAME_DIR/game/ffxiv_dx11.exe" ]; then
  mkdir -p conf
  if [ -f conf/global.properties ]; then
    tmp_file="$(mktemp)"
    awk -v game_path="$GAME_DIR" '
      BEGIN { done = 0 }
      /^GamePath=/ {
        print "GamePath=" game_path
        done = 1
        next
      }
      { print }
      END {
        if (!done) print "GamePath=" game_path
      }
    ' conf/global.properties > "$tmp_file"
    mv "$tmp_file" conf/global.properties
  else
    cat > conf/global.properties <<EOF
GamePath=$GAME_DIR
DLanguage=CHS
SLanguage=EN
ReplaText=1
FLanguage=CSV
ReplaFont=1
SkipFiles=exd/BNpcName
TransMode=0
EOF
  fi
fi

exec java -jar "$SCRIPT_DIR/FFXIVChnTextPatch-SM.exe"
