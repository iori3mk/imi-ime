// IMi：候補の窓と意味の窓の配色（スタイル × 明るい・暗い）。
// 候補の窓（renderer/win32/imi_theme.cc）と、設定画面の見本（gui/config_dialog）で同じものを使う。

#ifndef MOZC_RENDERER_IMI_PALETTE_H_
#define MOZC_RENDERER_IMI_PALETTE_H_

#include <cstdint>

namespace mozc {
namespace renderer {

// 色は 0xRRGGBB
struct ImiPalette {
  uint32_t window_bg;
  uint32_t border;
  uint32_t text;
  uint32_t sub_text;
  uint32_t focus_bg;
  uint32_t accent;
  uint32_t separator;
  bool serif;  // 既定の字体が明朝体か（でなければゴシック体）
};

// style：0 和紙と墨、1 すっきり、2 夜（config.proto の ImiWindowStyle）
inline ImiPalette GetImiPalette(int style, bool dark) {
  if (style == 2) {
    return {0x23262C, 0x3A3E46, 0xE8E6E1, 0x9A9890, 0x343A44, 0xE8A25C, 0x343840, true};
  }
  if (style == 1) {
    return dark ? ImiPalette{0x202225, 0x3A3D42, 0xE6E6E6, 0x9AA0A6, 0x2F3B4A, 0x7FB3E8, 0x34373C, false}
                : ImiPalette{0xFFFFFF, 0xDADDE2, 0x1B1B1B, 0x767676, 0xE1ECF8, 0x0F6CBD, 0xECEEF1, false};
  }
  return dark ? ImiPalette{0x1E1C1A, 0x3A3631, 0xEDE6DA, 0x9C9285, 0x3A342D, 0xD4785F, 0x34302B, true}
              : ImiPalette{0xFBF8F1, 0xD8D0C2, 0x1E1B18, 0x8A8178, 0xEDE3D3, 0xB0402A, 0xE4DCCD, true};
}

}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_IMI_PALETTE_H_
