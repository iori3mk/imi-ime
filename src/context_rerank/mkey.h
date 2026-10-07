// 表を引く語の鍵（imi-dev（開発用リポジトリ）の phase0/ime_eval/mkey.py と同じ規則）。
// 文節の表記と読みから、末尾のひらがな・記号を同じ文字数だけ落とした「語幹|語幹の読み」を鍵にする。
//   使ったので／つかったので → 使|つか    アプリを／あぷりを → アプリ
// ひらがなだけの文節、数字を含む文節は鍵なし（空文字列）。

#ifndef MOZC_CONTEXT_RERANK_MKEY_H_
#define MOZC_CONTEXT_RERANK_MKEY_H_

#include <string>

#include "absl/strings/string_view.h"

namespace mozc::context_rerank {

std::string MakeKey(absl::string_view surface, absl::string_view reading);

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_MKEY_H_
