// 前の文脈（確定した文節の鍵と、確定した文字列）を覚えておく（IMi）。
// IME では確定のたびに Commit で足していく。検証（converter_main）では Set で直接与える。
// 覚えるのはプロセスに1つだけ。変換に使うときは Snapshot で、アプリから受け取った
// カーソルの前の文字（入力を始めたときのもの）と突き合わせ、本当に今の位置の前にある分だけを使う。

#ifndef MOZC_CONTEXT_RERANK_CONTEXT_STORE_H_
#define MOZC_CONTEXT_RERANK_CONTEXT_STORE_H_

#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/synchronization/mutex.h"

namespace mozc::context_rerank {

class ContextStore {
 public:
  static ContextStore& Get();
  // 検証用：前の文脈を置き換える（keys の空文字列は鍵なしの文節）
  void Set(std::vector<std::string> keys, std::string text);
  void SetKeys(std::vector<std::string> keys);
  void SetText(std::string text);
  // 確定した文節（表記, 読み）を足す
  void Commit(const std::vector<std::pair<std::string, std::string>>& segments);
  void Clear();
  std::vector<std::string> keys() const;
  std::string text() const;  // 確定した文字列（末尾から最大200文字）

  // 変換に使う前の文脈。
  // アプリからカーソルの前の文字を受け取れたとき（has_preceding）：文字列はそれを使い、鍵は
  //   覚えている文節のうち、その文字列の末尾に（空白を挟んでもよい）続けて並んでいる分だけを使う。
  //   別のアプリや入力欄に移ったとき、カーソルを動かしたとき、文を消したときは自然に外れる。
  // 受け取れないとき・空のとき：覚えている分を使う。ただし最後の確定から kIdleClear 経ったら忘れる。
  //   キーがそのままアプリに渡ったとき（Enter・矢印など）と、入力欄の切り替え（RESET_CONTEXT）で
  //   Clear される（session.cc）。
  void Snapshot(bool has_preceding, std::string_view preceding,
                std::vector<std::string>* keys, std::string* text);

  static constexpr std::chrono::minutes kIdleClear{5};

 private:
  struct Seg {
    std::string key;     // 鍵（作れない文節は空文字列）
    std::u32string value;  // 表記（Set で与えたときは空）
  };
  mutable absl::Mutex mu_;
  std::deque<Seg> segs_;
  std::u32string text_;
  std::chrono::steady_clock::time_point last_commit_;
};

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_CONTEXT_STORE_H_
