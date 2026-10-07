// IMi：候補の窓と意味の窓のスタイル（色・字体・大きさ）。
//
// 設定（config.proto の imi_window_style・imi_color_mode・imi_candidate_font・imi_font_size）と、
// Windows のアプリの明るい・暗いの設定から決める。
//   和紙と墨（明るい版・暗い版）、すっきり（明るい版・暗い版）、夜（いつも暗い）
// 描画の色は GetScaledRendererStyle（win32_dpi_util.cc）が ApplyImiTheme で差し替える。
// 設定ファイルが書き換わったら読み直す（Refresh）。変わったら generation() が増える。

#ifndef MOZC_RENDERER_WIN32_IMI_THEME_H_
#define MOZC_RENDERER_WIN32_IMI_THEME_H_

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "protocol/renderer_style.pb.h"

namespace mozc {
namespace renderer {
namespace win32 {

struct ImiTheme {
  COLORREF window_bg;
  COLORREF border;
  COLORREF text;
  COLORREF sub_text;   // 番号・読み・品詞など
  COLORREF focus_bg;   // 選んでいる候補の帯
  COLORREF accent;     // 意味の番号・選んでいる候補の番号
  COLORREF separator;  // 区切りの線
  bool dark;
  // 字体の候補（入っている最初のものを使う）
  std::vector<std::wstring> fonts;
  // 文字の大きさの倍率（小さい 0.88・ふつう 1.0・大きい 1.15）
  double font_scale;
  // 角の丸めと、選んでいる候補の帯の角の丸め（96dpi の px）
  int focus_radius;

  // 今のスタイル。初めて呼んだときと Refresh で決める
  static const ImiTheme& Current();
  // 設定ファイルと Windows の配色を確かめ、変わっていれば決め直す。変わったら true
  static bool Refresh();
  static uint64_t generation();
};

// 描画の色と大きさを、今のスタイルで差し替える
void ApplyImiTheme(RendererStyle* style);

// 字体の候補のうち、入っている最初のもの（なければ空）
std::wstring ImiThemeFontFace();

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_IMI_THEME_H_
