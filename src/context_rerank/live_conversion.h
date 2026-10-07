// IMi：同時変換（打鍵ごとに変換結果を表示し、Enter でそのまま確定する）の共通部品。
//
// - 設定：資料置き場（AssetDir）の rerank_config.txt に「live_conversion 1」があれば同時変換を行う。
// - 表示用の変換の間は ScopedLivePreview を置く。この間、ContextRerankRewriter は B（小型言語モデル）を
//   裏のスレッドで計算し、まだ終わっていなければ SetLivePending() で知らせる（表示は T0+K5 まで）。
//   クライアントは pending のあいだ短い間隔で問い合わせ（REFRESH_LIVE_CONVERSION）、B の結果で表示を差し替える。
// 変換を行うのはサーバーの1つのスレッドなので、印はスレッドごとに持つ。

#ifndef MOZC_CONTEXT_RERANK_LIVE_CONVERSION_H_
#define MOZC_CONTEXT_RERANK_LIVE_CONVERSION_H_

#include <string>

namespace mozc::context_rerank {

// 表・資料・設定の置き場所。環境変数 MOZC_CONTEXT_RERANK_DIR が優先。
// Windows で未設定なら mozc_server.exe と同じ場所の context_rerank。それ以外は空。
std::string AssetDir();

// rerank_config.txt の live_conversion が 1 なら true（最初の呼び出しで読む）
bool LiveConversionEnabled();
// 表示の安定化（前半の据え置き・B の判断の引き継ぎ）。rerank_config.txt の live_stabilize（既定 1）。
// 環境変数 MOZC_LIVE_STABILIZE があればそちらを使う（比較の実験用）
bool LiveStabilizeEnabled();
// 打鍵が止まってから前半の据え置きを解くまでのミリ秒。rerank_config.txt の live_hold_release_ms（既定 200）。
// 環境変数 MOZC_LIVE_HOLD_MS があればそちらを使う（比較の実験用）
int LiveHoldReleaseMs();

class ScopedLivePreview {
 public:
  ScopedLivePreview();
  ~ScopedLivePreview();
  ScopedLivePreview(const ScopedLivePreview&) = delete;
  ScopedLivePreview& operator=(const ScopedLivePreview&) = delete;
};

// 表示用の変換の最中か
bool InLivePreview();

// 打鍵ごとの予測候補（Suggest・Predict）を作る間だけ置く。この間は B を使わない（T0+K5 だけ）
class ScopedSkipLm {
 public:
  ScopedSkipLm();
  ~ScopedSkipLm();
  ScopedSkipLm(const ScopedSkipLm&) = delete;
  ScopedSkipLm& operator=(const ScopedSkipLm&) = delete;
};
bool SkipLm();

// IME の Space の変換（engine_converter の Convert）の間だけ置く。この間は B を待つ時間に上限
// （rerank_config.txt の lm_budget_ms）を設け、間に合わなければ SetLivePending() で知らせる。
// converter_main などの評価では置かないので、B を最後まで計算する
class ScopedLmBudget {
 public:
  ScopedLmBudget();
  ~ScopedLmBudget();
  ScopedLmBudget(const ScopedLmBudget&) = delete;
  ScopedLmBudget& operator=(const ScopedLmBudget&) = delete;
};
bool InLmBudget();
// B がまだ終わっていないことを知らせる（表示用の変換の中で呼ぶ）
void SetLivePending();
// 知らせを読んで下ろす
bool TakeLivePending();

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_LIVE_CONVERSION_H_
