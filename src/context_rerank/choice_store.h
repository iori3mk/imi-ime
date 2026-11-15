// IMi：利用者が直した入れ替えを覚える。
//
// 文脈による並べ替えと小型言語モデル（B）は、Mozc の学習が第1候補に上げた語も選び直す。そのため、
// 出た候補（斎藤）を利用者が別の候補（斉藤）に直して確定しても、前の文によっては次も同じ候補に戻っていた。
// そこで「この読みで、A の代わりに B を選んだ」という組を覚え、B が第1候補のときに A へは入れ替えない。
// のちに利用者が逆向き（B の代わりに A）にも直したら、両方を使う語の組（公開・後悔など）とみなし、
// それからは今までどおり文脈で選ぶ。
//
// 覚えるのは Mozc の学習履歴と同じ形（語そのものではなく、組から作った数値）で、設定の場所の
// imi_choice.db に置く。件数には上限があり、古いものから消える。

#ifndef MOZC_CONTEXT_RERANK_CHOICE_STORE_H_
#define MOZC_CONTEXT_RERANK_CHOICE_STORE_H_

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "storage/lru_storage.h"

namespace mozc::context_rerank {

class ChoiceStore {
 public:
  // filename：記録のファイル。開けなければ何も覚えない（Prefers はいつも false）
  explicit ChoiceStore(const std::string& filename);

  // reading の読みで、出ていた rejected の代わりに chosen を選んだ
  void Learn(absl::string_view reading, absl::string_view chosen, absl::string_view rejected);
  // winner を loser に入れ替えてはいけないか（loser の代わりに winner を選んだことがあり、逆はない）
  bool Prefers(absl::string_view reading, absl::string_view winner, absl::string_view loser) const;
  // 全部忘れる（学習履歴の消去）
  void Clear();

 private:
  std::unique_ptr<storage::LruStorage> storage_;
};

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_CHOICE_STORE_H_
