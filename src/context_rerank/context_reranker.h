// T0+K5：前後の文脈と隣の語の共起の表で、各文節の候補を並べ替える（IMi）。
// imi-dev（開発用リポジトリ）の phase0/ime_eval/mrerank.py（rerank_mk）と同じ手順。Mozc の型に依存しない形にして、単体で照合できるようにする。
//
// 表（imi-dev（開発用リポジトリ）の phase0/corpus/mozc_tables.py が作り、export で TSV にする）
//   cooc_pairs.tsv  対象語\t文脈語\t共起数      cooc_bg.tsv  文脈語\t数      cooc_stop.tsv  語
//   adj_left.tsv / adj_right.tsv  対象語\t隣の語\t数      adj_n.tsv  T|L|R\t語\t数

#ifndef MOZC_CONTEXT_RERANK_CONTEXT_RERANKER_H_
#define MOZC_CONTEXT_RERANK_CONTEXT_RERANKER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "base/mmap.h"

namespace mozc::context_rerank {

struct SegmentInput {
  std::string reading;
  std::vector<std::string> values;  // 候補の表記（上位から）
  std::vector<int32_t> costs;       // 候補のコスト（values と同じ長さ）
};

class ContextReranker {
 public:
  // dir に tables.bin（imi-dev（開発用リポジトリ）の phase0/corpus/mozc_tables.py export が書く）があればそれを、なければ TSV を読む
  bool Load(const std::string& dir);
  // 各文節で選ぶ候補の番号を返す。ctx_keys：前の文脈（確定した文節）の鍵（空文字列は鍵なし）
  std::vector<int> Rerank(const std::vector<SegmentInput>& segs,
                          const std::vector<std::string>& ctx_keys,
                          double lam, double lam2, int passes = 2) const;
  size_t num_pairs() const { return num_pairs_; }

 private:
  static constexpr uint32_t kNone = UINT32_MAX;
  uint32_t Id(absl::string_view key) const;
  bool Usable(uint32_t w) const;  // 文脈語として使える（背景分布にあり、ストップ語でない）
  // 共起の表の対象語か（rerank.py の「target in model.pairs」。共起が0件の対象語も含む）
  bool InCooc(uint32_t t) const { return t != kNone && t < has_row_.size() && has_row_[t]; }
  bool KnownAdj(uint32_t t) const { return t != kNone && t < n_target_.size() && n_target_[t] > 0; }
  double CoocScore(uint32_t t, const std::vector<uint32_t>& words) const;
  double AdjScore(uint32_t t, uint32_t left, uint32_t right) const;
  // 対象語 → （語, 数）を語の番号順に並べた表。TSV から読んだときは owned に持ち、tables.bin のときは
  // 写像したファイルの中（off：行の始まり K+1 個、flat：（語, 数）の組）を直接引く（IMi S4：メモリを減らす）
  struct Rows {
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> owned;
    const char* off = nullptr;
    const char* flat = nullptr;
    uint32_t n = 0;  // 写像のときの行の数
    uint32_t Lookup(uint32_t t, uint32_t w) const;
  };
  uint32_t Intern(absl::string_view key);
  bool LoadTsv(const std::string& dir);
  bool LoadBinary(const std::string& path);

  absl::flat_hash_map<std::string, uint32_t> ids_;        // TSV のとき
  absl::flat_hash_map<absl::string_view, uint32_t> view_ids_;  // tables.bin のとき（写像したファイルの中の鍵）
  std::unique_ptr<Mmap> mmap_;
  // T0
  Rows pairs_;  // 対象語 → （文脈語, 共起数）を文脈語の番号順に
  std::vector<double> n_t_;      // 対象語の共起数の合計
  std::vector<bool> has_row_;
  std::vector<double> log_pb_;   // 文脈語の背景確率の対数（背景分布にない語は NaN）
  std::vector<bool> stop_;
  // K5
  Rows left_, right_;
  std::vector<double> n_target_, n_left_, n_right_;
  double total_target_ = 0;
  size_t num_pairs_ = 0;
  double a_ = 1.0;  // 平滑化の擬似数（T0・K5 とも1）
};

// Mozc のコストを順位どおりに単調にする（rerank.monotone_costs と同じ）
std::vector<double> MonotoneCosts(const std::vector<int32_t>& costs);

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_CONTEXT_RERANKER_H_
