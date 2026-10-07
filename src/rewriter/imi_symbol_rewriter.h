// IMi：記号1文字の変換では、学習で第1候補を変えない（2番目以降は学習の順のまま）。
//
// 「・」を一度「／」に変換すると、Mozc の学習（UserSegmentHistoryRewriter など）で次から「／」が第1候補に
// なり、「・」のつもりで打った記号が別の記号になってしまう（利用者の指摘、2026-10-07）。
// 文節の読みが記号1文字のときは、学習の処理の前の第1候補を覚えておき（ImiSymbolMarkRewriter。選んだ候補の
// 学習の UserSegmentHistoryRewriter より前に置く）、書き換えの処理の最後で第1候補に戻す（ImiSymbolRewriter）。打った字と同じ候補を
// 目印にしないのは、「!」の第1候補が全角の「！」のように、学習なしでも読みと違う表記が第1候補になるため。
// 全角・半角を前回の形に合わせる仕組み（CharacterFormManager の LAST_FORM）は、記号1文字では使わず、
// 学習もしない（variants_rewriter.cc・user_history_predictor.cc の「IMi」）。

#ifndef MOZC_REWRITER_IMI_SYMBOL_REWRITER_H_
#define MOZC_REWRITER_IMI_SYMBOL_REWRITER_H_

#include "converter/segments.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

namespace mozc {

// 学習の前の第1候補を覚える（同じスレッドで続けて呼ばれる ImiSymbolRewriter が読む）
class ImiSymbolMarkRewriter : public RewriterInterface {
 public:
  int capability(const ConversionRequest& request) const override {
    return RewriterInterface::CONVERSION;
  }
  bool Rewrite(const ConversionRequest& request, Segments* segments) const override;
};

class ImiSymbolRewriter : public RewriterInterface {
 public:
  int capability(const ConversionRequest& request) const override {
    return RewriterInterface::CONVERSION;
  }
  bool Rewrite(const ConversionRequest& request, Segments* segments) const override;
};

}  // namespace mozc

#endif  // MOZC_REWRITER_IMI_SYMBOL_REWRITER_H_
