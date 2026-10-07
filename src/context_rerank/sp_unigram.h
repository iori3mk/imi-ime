// rinna/japanese-gpt2 の sentencepiece（unigram 方式）と同じトークン列を作る。
// sentencepiece ライブラリを使わない自前の実装（IMi）。
//
// 資料は imi-dev（開発用リポジトリ）の phase0/corpus/export_lm_assets.py で書き出す。
//   pieces.tsv  番号\t得点\t表記
//   norm.tsv    16進の符号位置\t置き換え後の文字列（nmt_nfkc で変わる文字だけ）
// 処理：文字の置き換え → 空白をまとめて「▁」にし先頭にも「▁」を付ける → 得点の和が最大の分け方（Viterbi）。
// 辞書にない文字は「不明」にし、続く不明はまとめる。不明の出力番号は transformers に合わせて変えられる。

#ifndef MOZC_CONTEXT_RERANK_SP_UNIGRAM_H_
#define MOZC_CONTEXT_RERANK_SP_UNIGRAM_H_

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace mozc::context_rerank {

class SpUnigram {
 public:
  // unk_output_id：不明トークンとして出力する番号（transformers の T5Tokenizer は </s> の番号を出す）。
  bool Load(const std::string& pieces_path, const std::string& norm_path,
            int unk_id, int unk_output_id);
  std::vector<int> Encode(absl::string_view text) const;
  std::u32string Normalize(absl::string_view text) const;

 private:
  absl::flat_hash_map<std::u32string, std::pair<int, float>> pieces_;
  absl::flat_hash_map<char32_t, std::u32string> norm_;
  size_t max_piece_len_ = 0;
  float unk_score_ = 0;
  int unk_id_ = 0;
  int unk_output_id_ = 0;
};

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_SP_UNIGRAM_H_
