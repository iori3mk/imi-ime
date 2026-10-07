#!/usr/bin/env bash
# IMi のインストーラー（imi/out/IMi_<版>.msi）を Windows で作る。Git Bash で実行する：
#   bash imi/build_msi_win.sh
# 前提（README.md の「ビルド」）：VS 2022（C++・ATL）、Python 3.12 以上、Bazelisk、.NET 8 SDK、
# src で update_deps.py と build_qt.py を済ませておく。表とモデルがなければ imi/fetch_assets.sh で取得する。
#
# 1. version.bzl の BUILD_OSS を1つ上げる。Windows のインストーラーは同じ版のファイルを上書きしないので、
#    上げないと上書きインストールで変換エンジンが古いまま残る（REVISION は開発版では 100 に固定で効かない）
# 2. 配布物（imi/package：onnxruntime.dll と context_rerank/）を組み立てる
# 3. 配布物入りでビルドする（build_installer.py が MOZC_CONTEXT_RERANK_PACKAGE を見て MSI に入れる）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src"
ASSETS="${IMI_ASSETS:-$ROOT/imi/assets}"
PKG="$ROOT/imi/package"
BAZELISK="${BAZELISK:-bazelisk}"
export MSYS_NO_PATHCONV=1
[ -d "$LOCALAPPDATA/Microsoft/dotnet" ] && export PATH="$LOCALAPPDATA/Microsoft/dotnet:$PATH" DOTNET_ROOT="$LOCALAPPDATA/Microsoft/dotnet"
bz() { "$BAZELISK" --nowindows_enable_symlinks "$@"; }  # 開発者モードでなくてもビルドできるように

[ -f "$ASSETS/tables.bin" ] || bash "$ROOT/imi/fetch_assets.sh"

VER=$(sed -n 's/^BUILD_OSS = \([0-9]*\)$/\1/p' "$SRC/version.bzl")
VER=$((VER + 1))
sed -i "s/^BUILD_OSS = [0-9]*$/BUILD_OSS = $VER/" "$SRC/version.bzl"
echo "BUILD_OSS: $VER"

cd "$SRC"
# ONNX Runtime（MODULE.bazel の http_archive）を取得し、DLL を配布物に入れる
bz build --config release_build //context_rerank:lm_scorer
# info の前に Bazelisk が設定の行（BAZEL_LLVM=… など）を出すことがあるので、最後の行だけを使う
OUTPUT_BASE=$(bz info --config release_build output_base | tail -1 | tr -d '\r')
# 取得した外部の配布物はリンクになっていることがあり find がたどらないので、名前で探す
DLL=""
for d in "$OUTPUT_BASE"/external/*onnxruntime_win; do
  [ -f "$d/lib/onnxruntime.dll" ] && DLL="$d/lib/onnxruntime.dll"
done
[ -f "$DLL" ] || { echo "onnxruntime.dll が見つかりません" >&2; exit 1; }
rm -rf "$PKG"
mkdir -p "$PKG/context_rerank"
cp "$DLL" "$PKG/"
for f in tables.bin basep_int8s.onnx alt_int8s.onnx lm_shared.bin wikt_dict.bin verb_class.tsv pieces.tsv norm.tsv config.json bos_kv.f32 bos_lp.f32; do
  cp "$ASSETS/$f" "$PKG/context_rerank/"
done
cp "$ROOT/imi/rerank_config.txt" "$PKG/context_rerank/"

bz build package --config release_build "--action_env=MOZC_CONTEXT_RERANK_PACKAGE=$(cygpath -w "$PKG")"
mkdir -p "$ROOT/imi/out"
MAJOR=$(sed -n 's/^MAJOR = \([0-9]*\)$/\1/p' version.bzl)
MINOR=$(sed -n 's/^MINOR = \([0-9]*\)$/\1/p' version.bzl)
cp bazel-bin/win32/installer/Mozc64.msi "$ROOT/imi/out/IMi_$MAJOR.$MINOR.$VER.msi"
echo "作りました: imi/out/IMi_$MAJOR.$MINOR.$VER.msi"
