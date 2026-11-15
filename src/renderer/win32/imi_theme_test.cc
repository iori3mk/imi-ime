// IMi：字体が入っているかの確かめ。字体の名前の上限（31 文字）を超える名前でプログラムが止まらないこと

#include "renderer/win32/imi_theme.h"

#include <windows.h>

#include <string>

#include "testing/gunit.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

TEST(ImiThemeTest, InstalledFont) {
  EXPECT_TRUE(ImiIsFontInstalled(L"Arial"));
  EXPECT_FALSE(ImiIsFontInstalled(L"IMi No Such Font"));
}

TEST(ImiThemeTest, EmptyFontName) { EXPECT_FALSE(ImiIsFontInstalled(L"")); }

TEST(ImiThemeTest, TooLongFontName) {
  // 31 文字は入る長さ、32 文字からは入らない長さ
  EXPECT_FALSE(ImiIsFontInstalled(std::wstring(LF_FACESIZE - 1, L'A')));
  EXPECT_FALSE(ImiIsFontInstalled(std::wstring(LF_FACESIZE, L'A')));
  EXPECT_FALSE(ImiIsFontInstalled(std::wstring(200, L'A')));
}

}  // namespace
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
