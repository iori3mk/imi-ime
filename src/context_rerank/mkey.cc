#include "context_rerank/mkey.h"

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc::context_rerank {
namespace {

// ime_eval/mkey.py の HAS_KANJI：[一-鿿々〆ヶ]
bool IsKanji(char32_t c) {
  return (c >= 0x4E00 && c <= 0x9FFF) || c == U'々' || c == U'〆' || c == U'ヶ';
}
bool IsDigit(char32_t c) {
  return (c >= U'0' && c <= U'9') || (c >= U'０' && c <= U'９');
}
// KATAKANA：[ァ-ヺー]
bool IsKatakana(char32_t c) { return (c >= U'ァ' && c <= U'ヺ') || c == U'ー'; }
// TRAIL：[ぁ-ゖ、。，．！？!?」』）)…・〜～\s]
bool IsTrail(char32_t c) {
  if (c >= U'ぁ' && c <= U'ゖ') return true;
  switch (c) {
    case U'、': case U'。': case U'，': case U'．': case U'！': case U'？':
    case U'!': case U'?': case U'」': case U'』': case U'）': case U')':
    case U'…': case U'・': case U'〜': case U'～':
    case U' ': case U'\t': case U'\n': case U'\r': case U'　':
      return true;
    default:
      return false;
  }
}
// LEAD：[「『（(\s]
bool IsLead(char32_t c) {
  switch (c) {
    case U'「': case U'『': case U'（': case U'(':
    case U' ': case U'\t': case U'\n': case U'\r': case U'　':
      return true;
    default:
      return false;
  }
}

}  // namespace

std::string MakeKey(absl::string_view surface_utf8, absl::string_view reading_utf8) {
  std::u32string s = Util::Utf8ToUtf32(surface_utf8);
  std::u32string r = Util::Utf8ToUtf32(reading_utf8);
  if (s.empty()) return "";
  for (char32_t c : s) {
    if (IsDigit(c)) return "";
  }
  size_t lead = 0;
  while (lead < s.size() && IsLead(s[lead])) ++lead;
  if (lead > 0 && r.compare(0, lead, s, 0, lead) == 0) {
    s = s.substr(lead);
    r = r.substr(lead);
  }
  size_t n = 0;
  while (n < s.size() && IsTrail(s[s.size() - 1 - n])) ++n;
  const std::u32string stem = s.substr(0, s.size() - n);
  if (stem.empty()) return "";
  bool kanji = false, kata = true;
  for (char32_t c : stem) {
    kanji |= IsKanji(c);
    kata &= IsKatakana(c);
  }
  if (kanji) {
    std::u32string sr = r;
    if (n > 0 && r.size() >= n &&
        r.compare(r.size() - n, n, s, s.size() - n, n) == 0) {
      sr = r.substr(0, r.size() - n);
    }
    return Util::Utf32ToUtf8(stem) + "|" + Util::Utf32ToUtf8(sr);
  }
  if (stem.size() >= 2 && kata) return Util::Utf32ToUtf8(stem);
  return "";
}

}  // namespace mozc::context_rerank
