#include "rewriter/imi_symbol_rewriter.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "config/character_form_manager.h"
#include "converter/segments.h"
#include "request/conversion_request.h"

namespace mozc {
namespace {

// 記号1文字の文節の（読み, 学習の前の第1候補）。文節の順
thread_local std::vector<std::pair<std::string, std::string>> g_marked;

}  // namespace

bool ImiSymbolMarkRewriter::Rewrite(const ConversionRequest& request, Segments* segments) const {
  g_marked.clear();
  for (const Segment& segment : segments->conversion_segments()) {
    if (!config::CharacterFormManager::ImiIsSingleSymbol(segment.key()) || segment.candidates_size() == 0) continue;
    g_marked.emplace_back(segment.key(), segment.candidate(0).value);
  }
  return false;
}

bool ImiSymbolRewriter::Rewrite(const ConversionRequest& request, Segments* segments) const {
  bool changed = false;
  size_t next = 0;
  for (Segment& segment : segments->conversion_segments()) {
    if (!config::CharacterFormManager::ImiIsSingleSymbol(segment.key()) || segment.candidates_size() < 2) continue;
    // 学習の処理のあいだに文節の区切りが変わったときは、読みが合うものだけを使う
    while (next < g_marked.size() && g_marked[next].first != segment.key()) ++next;
    if (next >= g_marked.size()) break;
    const std::string& top = g_marked[next++].second;
    if (segment.candidate(0).value == top) continue;
    for (size_t c = 1; c < segment.candidates_size(); ++c) {
      if (segment.candidate(static_cast<int>(c)).value == top) {
        segment.move_candidate(static_cast<int>(c), 0);
        changed = true;
        break;
      }
    }
  }
  g_marked.clear();
  return changed;
}

}  // namespace mozc
