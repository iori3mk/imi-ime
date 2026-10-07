#!/usr/bin/env bash
# IMi の表と小型言語モデル（約330MB）を、このリポジトリの GitHub Releases から imi/assets/ に取得する。
#   bash imi/fetch_assets.sh [タグ]
# GitHub CLI（gh）でログインしておく（リポジトリが非公開のため）。
# 中身：tables.bin（T0+K5 の表）、basep_int8s.onnx・alt_int8s.onnx・lm_shared.bin（rinna/japanese-gpt2-xsmall の int8 版。重みは lm_shared.bin で共有）、
#      pieces.tsv・norm.tsv・config.json・bos_kv.f32・bos_lp.f32（トークン化と文頭の状態）
# 作り方は開発用リポジトリ（imi-dev）の phase0/README.md と 引継ぎ/引継ぎ書.md。
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-assets-2026.10.07}"
mkdir -p "$ROOT/imi/assets"
gh release download "$TAG" --repo iori3mk/imi-ime --dir "$ROOT/imi/assets" --clobber
for f in tables.bin basep_int8s.onnx alt_int8s.onnx lm_shared.bin wikt_dict.bin verb_class.tsv pieces.tsv norm.tsv config.json bos_kv.f32 bos_lp.f32; do
  [ -s "$ROOT/imi/assets/$f" ] || { echo "取得できませんでした: $f（gh auth status と、タグ $TAG を確かめる）" >&2; exit 1; }
done
ls -la "$ROOT/imi/assets"
