#!/usr/bin/env bash
# IMi のリリースを GitHub に「下書き」で作る（公開は GitHub の画面で確かめてから行う）。
#   bash imi/release_github.sh imi/out/IMi_3.34.<版>.msi 変更点.md
# 変更点のファイルには、利用者向けの変更点を Markdown の箇条書きで書く。
# リリースの本文には、変更点・インストールの手順（警告の画面の説明）・SHA-256 を入れる。
set -euo pipefail
MSI="$1"
CHANGES="$2"
[ -f "$MSI" ] || { echo "MSI がありません: $MSI" >&2; exit 1; }
[ -f "$CHANGES" ] || { echo "変更点のファイルがありません: $CHANGES" >&2; exit 1; }
NAME="$(basename "$MSI")"
VER="${NAME#IMi_}"
VER="${VER%.msi}"
TAG="v$VER"
SHA="$(sha256sum "$MSI" | cut -d' ' -f1 | tr 'a-f' 'A-F')"
SIZE_MB="$(( $(stat -c %s "$MSI") / 1024 / 1024 ))"
NOTES="$(mktemp)"
UP="$(mktemp -d)"
trap 'rm -rf "$NOTES" "$UP"' EXIT
# 配るファイルの名前は版の番号を付けず IMi.msi に固定する。README のダウンロードのリンク
# （releases/latest/download/IMi.msi）がいつも最新の版を指すように
cp "$MSI" "$UP/IMi.msi"
cat > "$NOTES" <<EOF
## 変更点

$(cat "$CHANGES")

## ダウンロード

- \`IMi.msi\`（版 $VER、${SIZE_MB}MB）
- SHA-256：\`$SHA\`

対応：Windows 10・11（64ビット）。

## インストール

1. 下の「Assets」の \`IMi.msi\` を押してダウンロードし、ダブルクリックします。
2. ブラウザが「一般的にダウンロードされていません」などと警告したら、ダウンロードの一覧の「…」→「保持する」を押します。
3. 「Windows によって PC が保護されました」と出たら、「詳細情報」→「実行」を押します。
4. 「このアプリがデバイスに変更を加えることを許可しますか？」と出たら、「はい」を押します。
5. 終わったら PC を再起動します。

警告は、インストーラーに電子署名を付けていないために出ます。

使い方は [README](https://github.com/iori3mk/imi-ime#readme) を見てください。
EOF
gh release create "$TAG" "$UP/IMi.msi" --repo iori3mk/imi-ime --target main --draft --title "IMi $VER" --notes-file "$NOTES"
echo "下書きを作りました: $TAG（GitHub のリリースのページで確かめてから公開してください）"
