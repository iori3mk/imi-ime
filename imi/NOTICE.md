# 使っている外部のものと、その条件

| もの | 使い方 | 条件 |
| --- | --- | --- |
| IMi で加えた部分 | 文脈による選び直し・同時変換・画面など | BSD 3-Clause（Copyright 2026 iori。[LICENSE](../LICENSE)） |
| [Mozc](https://github.com/google/mozc) | IMi の元 | BSD 3-Clause（[LICENSE](../LICENSE)） |
| Mozc の辞書（IPAdic、沖縄辞書ほか） | 変換の辞書（`src/data/dictionary_oss/`） | それぞれの条件（`src/data/dictionary_oss/README.txt`。表示を残せば改変・再配布可） |
| [日本語用例辞書](https://github.com/hiroyuki-komatsu/japanese-usage-dictionary) | 候補の横の「用例」 | BSD 3-Clause |
| [Qt](https://www.qt.io/) 6.9.1（qtbase） | 設定画面・辞書ツールなど（DLL を同梱） | LGPL v3（DLL を差し替えられる形で同梱。[ソース](https://download.qt.io/archive/qt/6.9/6.9.1/submodules/qtbase-everywhere-src-6.9.1.tar.xz)。求めに応じても渡す） |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) 1.30.0 | 小型言語モデルの計算（`onnxruntime.dll` を同梱） | MIT |
| [rinna/japanese-gpt2-xsmall](https://huggingface.co/rinna/japanese-gpt2-xsmall) | 小型言語モデル（int8 に変換して同梱） | MIT |
| [SudachiDict](https://github.com/WorksApplications/SudachiDict) | 辞書に加えた名詞（`src/data/dictionary_oss/dictionary09.txt`） | Apache License 2.0（名詞だけを取り出し、Mozc の辞書の形式に変えた） |
| [Wikipedia 日本語版](https://ja.wikipedia.org/) | 共起と隣接の表（`tables.bin`）の元になった文 | CC BY-SA 4.0 |
| [ウィクショナリー日本語版](https://ja.wiktionary.org/) | 候補の横に出す語の意味（`wikt_dict.bin`。意味の行を取り出し、記法を除いて辞書の形にした） | CC BY-SA（ウィクショナリーの条件に合わせ、`wikt_dict.bin` も CC BY-SA で提供） |

- 表（`tables.bin`・`verb_class.tsv`）は、Wikipedia 日本語版の文から数えた語の組の出現回数で、文そのものは含みません。Wikipedia の条件に合わせて **CC BY-SA 4.0** で提供します（出典：Wikipedia 日本語版の執筆者）。
- 小型言語モデルは、元のモデルの重みを int8 に変換したものです（作り方は開発用リポジトリ imi-dev の `phase0/ime_eval/lm_onnx.py`）。
