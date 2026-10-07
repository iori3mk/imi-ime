// IMi：文脈による候補の並べ替え（T0+K5）と、小型言語モデル（B）を、Mozc の書き換え処理の最後で行う。
//
// 環境変数 MOZC_CONTEXT_RERANK_DIR のディレクトリ（Windows で未設定なら mozc_server.exe と同じ場所の
// context_rerank）から、表（tables.bin または cooc_*.tsv・adj_*.tsv）、B の資料
// （pieces.tsv・norm.tsv・basep_int8.onnx・alt_int8.onnx・bos_*.f32）、設定（rerank_config.txt）を読む。
// 設定や表が読めなければ何もしない（改造前の Mozc と同じ動作）。
//
// 手順は imi-dev の phase0 と同じ：
//   1. 各文節の候補（上位30件から表記の重複を除いた上位10件）を T0+K5 で並べ替える（ime_eval/mrerank.py）
//   2. 迷う文節（コスト差 margin 以内の上位8件に、漢字の鍵が2種類以上）の候補（最大 max_alt 件）を
//      入れ替えた文を B で採点し、現在の候補より delta 以上高い候補に入れ替える（corpus/b_speed.py）
// B は、同時変換の表示用の変換（live_conversion.h の ScopedLivePreview の中）では裏のスレッドで計算し、
// 結果をキャッシュに置く（表示は T0+K5 までで返し、次の問い合わせで B の結果を使う）。
// それ以外の変換（Space による変換、converter_main）はその場で計算する（キャッシュにあれば使う）。

#ifndef MOZC_CONTEXT_RERANK_CONTEXT_RERANK_REWRITER_H_
#define MOZC_CONTEXT_RERANK_CONTEXT_RERANK_REWRITER_H_

#include <algorithm>
#include <atomic>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

#include "context_rerank/context_reranker.h"
#include "context_rerank/lm_scorer.h"
#include "converter/segments.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

namespace mozc::context_rerank {

class ContextRerankRewriter : public RewriterInterface {
 public:
  ContextRerankRewriter();
  ~ContextRerankRewriter() override;
  int capability(const ConversionRequest& request) const override {
    return RewriterInterface::CONVERSION;
  }
  bool Rewrite(const ConversionRequest& request, Segments* segments) const override;
  void Finish(const ConversionRequest& request, const Segments& segments) override;

 private:
  struct Job {
    std::string key, prefix, base;
    std::vector<AltSet> alts;
  };
  bool GetLmScores(const std::string& prefix, const std::string& base,
                   const std::vector<AltSet>& alts, std::vector<AltScores>* scores) const;
  bool LookupCache(const std::string& key, std::vector<AltScores>* scores) const;
  bool VerbRatio(absl::string_view left, double* r) const;
  void MeasureSpeed();
  int MaxAlt() const { return light_ ? std::min(max_alt_, light_max_alt_) : max_alt_; }
  int Window() const { return light_ ? std::min(window_, light_window_) : window_; }
  void StoreCache(const std::string& key, std::vector<AltScores> scores) const;
  void WorkerLoop();

  std::unique_ptr<ContextReranker> reranker_;
  std::unique_ptr<LmScorer> lm_;
  double lam_ = 0.3, lam2_ = 1.5, delta_ = 2;
  int window_ = 4, margin_ = 4000, max_alt_ = 5, n_cand_ = 8, context_chars_ = 60;
  int lm_budget_ms_ = 80;
  // 「言」と「行」の選び直し：（名詞＋タブ＋助詞）→（話す系, 移動系）の数。verb_class.tsv がなければ空
  absl::flat_hash_map<std::string, std::pair<int, int>> verb_class_;
  double verb_theta_ = 0.5;
  // 遅い PC の軽い設定（MeasureSpeed）。lm_light_ms が 0 以下なら測らない
  double lm_light_ms_ = 0;
  int light_max_alt_ = 4, light_window_ = 2;
  std::atomic<bool> light_{false};  // Space の変換で B を待つ上限（ミリ秒）。0 以下ならその場で計算して待つ
  bool timing_ = false;

  // B の非同期計算（同時変換のとき）
  mutable absl::Mutex mu_;
  mutable std::list<std::pair<std::string, std::vector<AltScores>>> cache_ ABSL_GUARDED_BY(mu_);
  mutable std::optional<Job> next_job_ ABSL_GUARDED_BY(mu_);
  mutable std::string running_key_ ABSL_GUARDED_BY(mu_);
  bool stop_ ABSL_GUARDED_BY(mu_) = false;
  std::thread worker_;  // 最後に置く（他のメンバーを使うため）
};

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_CONTEXT_RERANK_REWRITER_H_
