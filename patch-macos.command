#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if ! command -v javac >/dev/null 2>&1; then
  echo "需要 JDK（javac），当前只找到 Java Runtime。"
  exit 1
fi

javac -encoding UTF-8 -cp FFXIVChnTextPatch-SM.exe MacPatchCli.java
java -cp ".:FFXIVChnTextPatch-SM.exe" MacPatchCli
