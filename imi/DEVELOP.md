# IMi の開発者向けの説明

使う人向けの説明は [README.md](../README.md) にあります。測定と開発の記録は開発用リポジトリ（imi-dev）にあります。

## Mozc との違い（ソースの場所）

- **文脈による候補の並べ替え（T0+K5）**：入力中の文の前後と、直前に確定した文の語を手がかりに、各文節の候補を並べ替えます。表は Wikipedia 日本語版と Tatoeba の日本語の文から数えた共起と隣接の数（`tables.bin`。写像して引く）。`src/context_rerank/context_reranker`
- **小型言語モデルによる選び直し（B）**：迷う文節は、rinna/japanese-gpt2-xsmall の int8 版（ONNX Runtime）で、候補を入れ替えた文の自然さを比べて選び直します。`src/context_rerank/lm_scorer`
- **前の文脈**：アプリから受け取ったカーソルの前の文字（TSF）を使い、受け取れないアプリでは確定した文を覚えておきます（5分で忘れる）。`src/context_rerank/context_store`
- **同時変換**：打っている間から変換して表示し、Enter でそのまま確定します。1文字で表示済みの前半が別の区切りに変わるときは、少しの間（`live_hold_release_ms`、既定 200ms）前半を前の表示のまま残します（据え置き）。据え置き中に Enter を押すと、据え置きを解いた表示（手を止めたら出る文）で確定するので、表示の前半が変わることがあります。B は裏のスレッドで計算し、終わり次第表示を差し替えます。`src/engine/engine_converter.cc` の `UpdateLivePreedit`、`src/win32/tip`（問い合わせのタイマー）
- **Space の変換の待ち時間の上限**：B が `lm_budget_ms` までに終わらなければ表の結果で候補を出し、終わったら差し替えます（候補を動かす前だけ）。
- **遅い PC での自動の切り替え**：起動のとき B の速さを測り、遅ければ候補の数と後ろの長さを減らします。
- **助詞の続き**：確定した語の続きに打った助詞だけの文節（「記入期間」＋「に」）は、ひらがなの助詞を第1候補にします。
- **記号1文字の第1候補**：「・」「「」「！」「￥」などの記号1文字の変換では、学習で第1候補を変えません（2番目以降は学習の順）。学習の前の第1候補を覚えて最後に戻し（`src/rewriter/imi_symbol_rewriter`）、全角・半角も前回の形に合わせず学習もしません（`variants_rewriter.cc`・`user_history_predictor.cc`・`character_form_manager` の「IMi」）。
- **「言」と「行」**：「先生に いって」「学校に いって」を、直前の名詞＋助詞の表（`verb_class.tsv`）で選び分けます。
- **辞書**：Mozc の辞書に、SudachiDict の名詞（`src/data/dictionary_oss/dictionary09.txt`）と、話し言葉の読み（「言う」の「ゆう」、`dictionary10.txt`）を加えています。
- **語の意味**：選んでいる候補の意味を、ウィクショナリー日本語版から作った辞書（`wikt_dict.bin`）で表示します。`src/context_rerank/wikt_dict`
- **キー**：入力中は無変換で半角英数、変換でひらがな。何も入力していないときは無変換で IME オフ、変換で IME オン。
- **画面**：候補の窓と意味の窓のスタイル（`src/renderer/win32/imi_theme`。設定と Windows の配色から色・字体・大きさを決める）、選んでいる候補の角を丸めた帯・差し色の番号・太字、意味の窓の辞書らしい描き方（`infolist_window.cc`）、設定画面（左に項目の一覧、カード、見た目のページの見本）、テキストボックスの外でも入力モードの表示とメニューを保つ（`src/win32/tip/tip_lang_bar_menu.cc`）、タスクバーが明るいとき（Windows モードがライト）は入力モードのアイコンを黒い文字にする（`*_light.ico` は `imi/icon/make_light_mode_icons.py` で作る）、製品のアイコン（`imi/icon`）。

## 設定

### 設定画面（IMi のページ）

| 項目（config.proto） | 既定 | 意味 |
| --- | --- | --- |
| `imi_live_conversion` | オン | 同時変換 |
| `imi_use_lm` | オン | 小型言語モデル（B） |
| `imi_use_context` | オン | 前の文脈 |
| `imi_show_candidates_on_convert` | オン | 1回目の Space から候補の一覧を出す |
| `imi_window_style` | 和紙と墨 | 候補の窓と意味の窓のスタイル（和紙と墨・すっきり・夜）。配色は `src/renderer/imi_palette.h` |
| `imi_color_mode` | Windows に合わせる | 明るい版・暗い版（夜はいつも暗い） |
| `imi_candidate_font`・`imi_font_size` | スタイルの既定・ふつう | 候補と意味の字体と大きさ |
| `imi_share_input_mode` | オン | 入力モード（あ・A）をすべてのアプリで共通にする。設定画面が `HKCU\Software\IMi` の `ShareInputMode` にも書き、IME の部品は最後の入力モードを `SharedOpen`・`SharedMode` に書いて入力欄を選んだときに合わせる（`src/win32/tip/imi_shared_mode`） |

### rerank_config.txt

インストール先の `C:\Program Files (x86)\Mozc\context_rerank\rerank_config.txt`（元は [rerank_config.txt](rerank_config.txt)）。ファイルを消すと Mozc と同じ動作になります。

| 項目 | 既定 | 意味 |
| --- | --- | --- |
| `lam`・`lam2` | 0.5・3.0 | 共起の表・隣接の表の重み |
| `delta` | 2 | B が今の候補より何点良ければ選び直すか |
| `window` | 4 | 入れ替えた箇所の後ろを何トークンまで点数に入れるか |
| `margin`・`max_alt`・`n_cand` | 4000・5・8 | B に渡す候補（Mozc のコストの差の上限・文節あたりの数・見る範囲） |
| `context_chars` | 60 | B に渡す前の文脈の文字数 |
| `use_lm` | 1 | 小型言語モデルを使う（0 で表だけ） |
| `lm_threads` | 2 | 小型言語モデルの計算のスレッド数 |
| `precision` | int8s | 小型言語モデルの版 |
| `live_conversion` | 1 | 同時変換の仕組み（0 で使わない） |
| `lm_budget_ms` | 80 | Space の変換で B を待つ上限（ミリ秒） |
| `lm_light_ms`・`light_max_alt`・`light_window` | 50・4・2 | 遅い PC の見分けのしきい値と、そのときの設定 |
| `ort_arena` | 0 | ONNX Runtime の作業用メモリの溜め置き（0 で切る。メモリ約100MB減） |
| `verb_theta` | 0.5 | 「言」と「行」を選び直すしきい値 |

### 調べもの用の記録

利用者の設定の場所（`%USERPROFILE%\AppData\LocalLow\Mozc`）に空のファイル `imi_ctx_debug` を置くと、前の文脈の扱いと確定した文字列を同じ場所の `imi_ctx.log` に書きます。アプリから受け取ったカーソルの前の文字や、入力した文がそのまま残るので、調べ終えたら `imi_ctx_debug` と `imi_ctx.log` を消してください。ファイルがあるかは変換エンジンの起動のあと最初に1回だけ確かめるので、置いたり消したりしたら PC を再起動するか、変換エンジン（`mozc_server.exe`）を止めてください。

## ビルド（Windows）

必要なもの（Mozc と同じ。[docs/build_mozc_in_windows.md](../docs/build_mozc_in_windows.md)）：Visual Studio 2022（C++ によるデスクトップ開発、ATL）、Python 3.12 以上、[Bazelisk](https://github.com/bazelbuild/bazelisk)、.NET 8 SDK、[GitHub CLI](https://cli.github.com/)（表とモデルの取得に使う）。

```sh
cd src
python build_tools/update_deps.py
python build_tools/build_qt.py --release --confirm_license
cd ..
bash imi/build_msi_win.sh        # Git Bash で。imi/out/IMi_<版>.msi ができる
```

- 表・小型言語モデル・語の意味の辞書はソースに含めず、このリポジトリの Releases（`assets-*` のタグ）に置いています。`imi/fetch_assets.sh` が `imi/assets/` に取得します（`build_msi_win.sh` が必要なら自動で呼ぶ）。
- ONNX Runtime 1.30.0 はビルドのときに公式の配布物を取得します（`src/MODULE.bazel`）。
- 開発者モードでない Windows では、Bazel に `--nowindows_enable_symlinks` が必要です（スクリプトは付けています）。
- 版は `src/version.bzl` の MAJOR.MINOR.BUILD_OSS です。インストーラーは、入っている IMi を版によらず置き換え、ファイルも毎回上書きします。

## 自動の確認（GitHub Actions）

`main` への push と Pull Request のたびに、GitHub の Windows のマシンで単体テストとインストーラーのビルドを行います（[.github/workflows/windows.yaml](../.github/workflows/windows.yaml)）。できたインストーラーは、その実行の Artifacts（`IMi-msi`、14日間）から取得できます。

## リリース

1. `src/version.bzl` の版を書き換えてコミットし、`main` に push します。
2. 版と同じ名前の注釈付きタグを付けて push します。タグの本文には、利用者向けの変更点を Markdown の箇条書きで書きます。
   ```sh
   git tag -a v1.1.1 -F 変更点.md
   git push origin v1.1.1
   ```
3. GitHub Actions がテストとビルドを行い、リリースを**下書き**で作ります（`IMi_<版>.msi`・SHA-256・変更点・インストールの手順）。タグと版が合わないときや、タグが注釈付きでないときは失敗します。
4. 下書きの `IMi_<版>.msi` を入れて確かめてから、GitHub の画面で公開します。

手元で作った MSI から下書きを作るときは `bash imi/release_github.sh <MSI> <変更点のファイル>` を使います。

## ブランチ

- `main`：IMi。Mozc の `c7538e6f`（2026-10-02）を元にしています。
- Mozc の更新を取り込むときは、`https://github.com/google/mozc.git` を `upstream` として追加し、`main` に取り込みます。
