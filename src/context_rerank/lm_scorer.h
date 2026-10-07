// B：小型言語モデル（rinna/japanese-gpt2-xsmall、ONNX Runtime、int8・文頭の分離）で、
// 迷う文節の候補を入れ替えた文の点数を出す（IMi）。
//
// imi-dev（開発用リポジトリ）の phase0/ime_eval/lm_onnx.py の OrtLMScorer（skip_bos=True）と同じ計算。
//   1. 現在の文を1回計算し（文頭の KV は保存した定数を使う）、内部状態（KV）を得る
//   2. 全文節の候補を入れ替えた文を1回の計算にまとめ、入れ替え箇所より前の KV を流用して残りだけ計算する
//   3. 入れ替え箇所の後ろは window トークンまでを点数に入れる
// 返す点数は文節ごとに定数がずれている（共通部分を省くため）が、文節内の候補どうしの差は正確。

#ifndef MOZC_CONTEXT_RERANK_LM_SCORER_H_
#define MOZC_CONTEXT_RERANK_LM_SCORER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "context_rerank/sp_unigram.h"

namespace Ort {
struct Env;
struct Session;
struct PrepackedWeightsContainer;
}  // namespace Ort

namespace mozc::context_rerank {

struct AltSet {
  int segment;                                       // 文節の番号
  std::vector<std::pair<int, std::string>> texts;    // （候補の番号, 候補を入れ替えた文）。番号0は現在の文
};

struct AltScores {
  int segment;
  std::vector<std::pair<int, float>> scores;  // （候補の番号, 点数）
};

class LmScorer {
 public:
  LmScorer();
  ~LmScorer();
  // assets：pieces.tsv・norm.tsv・basep_int8.onnx・alt_int8.onnx・bos_kv.f32・bos_lp.f32 のあるディレクトリ
  // precision：int8（既定）または fp32（basep_<precision>.onnx・alt_<precision>.onnx を読む）
  // use_arena：ONNX Runtime の作業用のメモリの溜め置きを使うか（rerank_config の ort_arena。既定は使わない）
  bool Load(const std::string& assets, int threads, const std::string& precision = "int8", bool use_arena = false);
  std::vector<AltScores> Score(absl::string_view prefix, absl::string_view base,
                               const std::vector<AltSet>& alts, int window) const;
  const SpUnigram& tokenizer() const { return sp_; }

 private:
  SpUnigram sp_;
  std::unique_ptr<Ort::Env> env_;
  // basep と alt は同じ重みを使う。並べ替え済みの重み（prepack）を2つのセッションで共有して、メモリを1つ分にする
  std::unique_ptr<Ort::PrepackedWeightsContainer> prepacked_;
  std::unique_ptr<Ort::Session> basep_;
  std::unique_ptr<Ort::Session> alt_;
  std::vector<float> bos_kv_;  // （層, 2, 1, 頭, 1, 次元）
  std::vector<float> bos_lp_;  // （語彙）
  int n_layer_ = 6, n_head_ = 8, head_dim_ = 64, bos_id_ = 1;
};

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_LM_SCORER_H_
