#include "context_rerank/sp_unigram.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/util.h"

namespace mozc::context_rerank {
namespace {

constexpr char32_t kSpace = U'▁';  // ▁
constexpr float kUnkPenalty = 10.0f;

// Python の str.isspace() と同じ範囲（norm.tsv はこれらを除いて書き出している）。
bool IsSpace(char32_t c) {
  return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 ||
         c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
         c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
         c == 0x3000;
}

}  // namespace

bool SpUnigram::Load(const std::string& pieces_path,
                     const std::string& norm_path, int unk_id,
                     int unk_output_id) {
  unk_id_ = unk_id;
  unk_output_id_ = unk_output_id;
  std::ifstream pf(pieces_path);
  if (!pf) return false;
  std::string line;
  float min_score = std::numeric_limits<float>::max();
  while (std::getline(pf, line)) {
    std::vector<absl::string_view> f = absl::StrSplit(line, '\t');
    if (f.size() != 3) continue;
    int id;
    float score;
    if (!absl::SimpleAtoi(f[0], &id) || !absl::SimpleAtof(f[1], &score)) {
      return false;
    }
    if (id == unk_id_) continue;
    std::u32string piece = Util::Utf8ToUtf32(f[2]);
    max_piece_len_ = std::max(max_piece_len_, piece.size());
    min_score = std::min(min_score, score);
    pieces_[piece] = {id, score};
  }
  unk_score_ = min_score - kUnkPenalty;
  std::ifstream nf(norm_path);
  if (!nf) return false;
  while (std::getline(nf, line)) {
    std::vector<absl::string_view> f = absl::StrSplit(line, '\t');
    if (f.size() != 2) continue;
    uint32_t cp;
    if (!absl::SimpleHexAtoi(f[0], &cp)) return false;
    norm_[static_cast<char32_t>(cp)] = Util::Utf8ToUtf32(f[1]);
  }
  return !pieces_.empty();
}

std::u32string SpUnigram::Normalize(absl::string_view text) const {
  // 文字の置き換え
  std::u32string mapped;
  for (char32_t c : Util::Utf8ToUtf32(text)) {
    if (IsSpace(c)) {
      mapped.push_back(U' ');
      continue;
    }
    auto it = norm_.find(c);
    if (it == norm_.end()) {
      mapped.push_back(c);
    } else {
      mapped += it->second;
    }
  }
  // 空白：前後を除き、続く空白は1つにまとめて「▁」にする。先頭に「▁」を付ける
  std::u32string out(1, kSpace);
  bool pending_space = false;
  for (char32_t c : mapped) {
    if (c == U' ' || IsSpace(c)) {
      pending_space = out.size() > 1;
      continue;
    }
    if (pending_space) out.push_back(kSpace);
    pending_space = false;
    out.push_back(c);
  }
  return out;
}

std::vector<int> SpUnigram::Encode(absl::string_view text) const {
  const std::u32string s = Normalize(text);
  const size_t n = s.size();
  constexpr float kNeg = -std::numeric_limits<float>::infinity();
  std::vector<float> best(n + 1, kNeg);
  std::vector<std::pair<size_t, int>> back(n + 1, {0, -1});  // (開始位置, 番号)
  best[0] = 0;
  for (size_t i = 0; i < n; ++i) {
    if (best[i] == kNeg) continue;
    bool single = false;
    const size_t max_len = std::min(max_piece_len_, n - i);
    for (size_t len = 1; len <= max_len; ++len) {
      auto it = pieces_.find(s.substr(i, len));
      if (it == pieces_.end()) continue;
      if (len == 1) single = true;
      const float cand = best[i] + it->second.second;
      if (cand > best[i + len]) {
        best[i + len] = cand;
        back[i + len] = {i, it->second.first};
      }
    }
    if (!single) {  // 1文字のトークンがない文字は「不明」
      const float cand = best[i] + unk_score_;
      if (cand > best[i + 1]) {
        best[i + 1] = cand;
        back[i + 1] = {i, unk_id_};
      }
    }
  }
  std::vector<int> ids;
  for (size_t pos = n; pos > 0; pos = back[pos].first) {
    ids.push_back(back[pos].second);
  }
  std::reverse(ids.begin(), ids.end());
  // 続く「不明」は1つにまとめ、出力用の番号にする
  std::vector<int> out;
  for (int id : ids) {
    if (id == unk_id_) {
      if (!out.empty() && out.back() == unk_output_id_) continue;
      out.push_back(unk_output_id_);
    } else {
      out.push_back(id);
    }
  }
  return out;
}

}  // namespace mozc::context_rerank
