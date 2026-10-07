// 語の意味の辞書（IMi）。ウィクショナリー日本語版から作った辞書（資料置き場の wikt_dict.bin）を
// 写像して読み、語（候補の内容語の表記）から見出しと説明を引く。作り方は imi-dev の
// phase0/corpus/wikt_dict.py。ファイルがなければ何も引けない（IME 自体は動く）。

#ifndef MOZC_CONTEXT_RERANK_WIKT_DICT_H_
#define MOZC_CONTEXT_RERANK_WIKT_DICT_H_

#include <cstdint>
#include <memory>

#include "absl/strings/string_view.h"
#include "base/mmap.h"

namespace mozc::context_rerank {

class WiktDict {
 public:
  // 資料置き場（AssetDir）の wikt_dict.bin を初めて使うときに読む
  static const WiktDict& Get();

  // key に一致する項目があれば、見出しと説明を返す
  bool Lookup(absl::string_view key, absl::string_view* title,
              absl::string_view* description) const;

  // 項目の通し番号（見つからなければ -1）。意味の窓で項目を見分けるのに使う
  int32_t Find(absl::string_view key) const;

 private:
  WiktDict();
  absl::string_view Str(uint32_t pos, uint32_t len) const;

  std::unique_ptr<Mmap> mmap_;
  const uint32_t* recs_ = nullptr;  // 項目ごとに6つ（鍵・見出し・説明の位置と長さ）
  uint32_t n_ = 0;
  const char* blob_ = nullptr;
  size_t blob_size_ = 0;
};

}  // namespace mozc::context_rerank

#endif  // MOZC_CONTEXT_RERANK_WIKT_DICT_H_
