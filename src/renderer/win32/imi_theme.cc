#include "renderer/win32/imi_theme.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "base/system_util.h"
#include "base/win32/wide_char.h"
#include "config/config_handler.h"
#include "protocol/config.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/imi_palette.h"

#pragma comment(lib, "advapi32.lib")

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

using config::Config;

struct State {
  std::mutex mu;
  ImiTheme theme;
  uint64_t generation = 0;
  std::filesystem::file_time_type config_time{};
  bool initialized = false;
  // 前回の決め手（同じなら決め直さない）
  int style = -1, color_mode = -1, font_size = -1;
  bool dark = false;
  std::string font;
};

State& GetState() {
  static State* s = new State();
  return *s;
}

// Windows のアプリの配色が暗いか（設定 → 個人用設定 → 色 →「既定のアプリ モード」）
bool WindowsAppsDark() {
  DWORD value = 1, size = sizeof(value);
  if (::RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS) {
    return false;
  }
  return value == 0;
}

std::filesystem::path ConfigPath() {
  return std::filesystem::path(::mozc::win32::Utf8ToWide(SystemUtil::GetUserProfileDirectory())) /
         L"config1.db";
}

constexpr COLORREF Hex(uint32_t rgb) {
  return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

const std::vector<std::wstring>& SerifFonts() {
  static const auto* f =
      new std::vector<std::wstring>{L"Noto Serif JP", L"Yu Mincho", L"BIZ UDPMincho Medium"};
  return *f;
}
const std::vector<std::wstring>& SansFonts() {
  static const auto* f = new std::vector<std::wstring>{L"BIZ UDPGothic", L"Noto Sans JP",
                                                       L"Yu Gothic UI", L"Meiryo UI"};
  return *f;
}

ImiTheme MakeTheme(int style, bool dark) {
  const ImiPalette p = GetImiPalette(style, dark);
  ImiTheme t{};
  t.window_bg = Hex(p.window_bg);
  t.border = Hex(p.border);
  t.text = Hex(p.text);
  t.sub_text = Hex(p.sub_text);
  t.focus_bg = Hex(p.focus_bg);
  t.accent = Hex(p.accent);
  t.separator = Hex(p.separator);
  t.dark = style == Config::IMI_STYLE_NIGHT || dark;
  t.fonts = p.serif ? SerifFonts() : SansFonts();
  t.font_scale = 1.0;
  t.focus_radius = p.serif ? 6 : 5;
  return t;
}

bool IsFontInstalled(const std::wstring& face) {
  LOGFONTW query = {};
  query.lfCharSet = DEFAULT_CHARSET;
  if (face.empty() || wcscpy_s(query.lfFaceName, face.c_str()) != 0) {
    return false;
  }
  bool found = false;
  const HDC dc = ::GetDC(nullptr);
  ::EnumFontFamiliesExW(
      dc, &query,
      [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) -> int {
        *reinterpret_cast<bool*>(found) = true;
        return 0;
      },
      reinterpret_cast<LPARAM>(&found), 0);
  ::ReleaseDC(nullptr, dc);
  return found;
}

// 呼ぶ側で mu を持つ
bool RefreshLocked(State& s) {
  std::error_code ec;
  const auto mtime = std::filesystem::last_write_time(ConfigPath(), ec);
  if (!ec && (!s.initialized || mtime != s.config_time)) {
    s.config_time = mtime;
    if (s.initialized) config::ConfigHandler::Reload();
  }
  const auto config = config::ConfigHandler::GetSharedConfig();
  const int style = config->imi_window_style();
  const int color_mode = config->imi_color_mode();
  const int font_size = config->imi_font_size();
  const std::string font = config->imi_candidate_font();
  const bool dark = color_mode == Config::IMI_COLOR_DARK ||
                    (color_mode == Config::IMI_COLOR_AUTO && WindowsAppsDark());
  if (s.initialized && style == s.style && color_mode == s.color_mode &&
      font_size == s.font_size && font == s.font && dark == s.dark) {
    return false;
  }
  s.style = style;
  s.color_mode = color_mode;
  s.font_size = font_size;
  s.font = font;
  s.dark = dark;
  ImiTheme t = MakeTheme(style, dark);
  if (!font.empty()) t.fonts.insert(t.fonts.begin(), ::mozc::win32::Utf8ToWide(font));
  t.font_scale = font_size == Config::IMI_FONT_SMALL   ? 0.88
                 : font_size == Config::IMI_FONT_LARGE ? 1.15
                                                       : 1.0;
  s.theme = std::move(t);
  s.initialized = true;
  ++s.generation;
  return true;
}

void SetColor(RendererStyle::RGBAColor* c, COLORREF v) {
  c->set_r(GetRValue(v));
  c->set_g(GetGValue(v));
  c->set_b(GetBValue(v));
  c->set_a(1.0);
}

}  // namespace

const ImiTheme& ImiTheme::Current() {
  State& s = GetState();
  std::lock_guard<std::mutex> lock(s.mu);
  if (!s.initialized) RefreshLocked(s);
  return s.theme;
}

bool ImiTheme::Refresh() {
  State& s = GetState();
  std::lock_guard<std::mutex> lock(s.mu);
  return RefreshLocked(s);
}

uint64_t ImiTheme::generation() {
  State& s = GetState();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.generation;
}

void ApplyImiTheme(RendererStyle* style) {
  const ImiTheme& t = ImiTheme::Current();
  SetColor(style->mutable_border_color(), t.border);
  SetColor(style->mutable_candidate_style()->mutable_background_color(), t.window_bg);
  SetColor(style->mutable_candidate_style()->mutable_foreground_color(), t.text);
  SetColor(style->mutable_shortcut_style()->mutable_background_color(), t.window_bg);
  SetColor(style->mutable_shortcut_style()->mutable_foreground_color(), t.sub_text);
  SetColor(style->mutable_description_style()->mutable_foreground_color(), t.sub_text);
  SetColor(style->mutable_footer_style()->mutable_foreground_color(), t.sub_text);
  SetColor(style->mutable_footer_sub_label_style()->mutable_foreground_color(), t.sub_text);
  style->clear_footer_border_colors();
  SetColor(style->add_footer_border_colors(), t.separator);
  SetColor(style->mutable_footer_top_color(), t.window_bg);
  SetColor(style->mutable_footer_bottom_color(), t.window_bg);
  SetColor(style->mutable_focused_background_color(), t.focus_bg);
  SetColor(style->mutable_focused_border_color(), t.focus_bg);
  SetColor(style->mutable_scrollbar_background_color(), t.window_bg);
  SetColor(style->mutable_scrollbar_indicator_color(), t.separator);
  auto scale = [&](RendererStyle::TextStyle* ts) {
    ts->set_font_size(static_cast<int>(ts->font_size() * t.font_scale + 0.5));
  };
  scale(style->mutable_candidate_style());
  scale(style->mutable_shortcut_style());
  scale(style->mutable_description_style());
  RendererStyle::InfolistStyle* info = style->mutable_infolist_style();
  SetColor(info->mutable_border_color(), t.border);
  SetColor(info->mutable_focused_background_color(), t.window_bg);
  SetColor(info->mutable_focused_border_color(), t.window_bg);
  SetColor(info->mutable_caption_background_color(), t.window_bg);
  SetColor(info->mutable_title_style()->mutable_foreground_color(), t.text);
  SetColor(info->mutable_description_style()->mutable_foreground_color(), t.text);
  SetColor(info->mutable_caption_style()->mutable_foreground_color(), t.sub_text);
  info->set_caption_string("");
  info->set_caption_height(0);
  // 見本 B に合わせた大きさ（96dpi の px。このあと GetScaledRendererStyle が dpi に合わせる）
  info->set_window_width(320);
  info->mutable_title_style()->set_font_size(20);
  info->mutable_description_style()->set_font_size(13);
  info->mutable_caption_style()->set_font_size(12);
  scale(info->mutable_title_style());
  scale(info->mutable_description_style());
}

std::wstring ImiThemeFontFace() {
  for (const std::wstring& f : ImiTheme::Current().fonts) {
    if (IsFontInstalled(f)) return f;
  }
  return L"";
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
