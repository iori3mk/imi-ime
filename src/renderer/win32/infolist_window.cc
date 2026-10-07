// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "renderer/win32/infolist_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

#include "base/coordinates.h"
#include "base/vlog.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/win32/imi_theme.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_dpi_util.h"

#pragma comment(lib, "dwmapi.lib")

namespace mozc {
namespace renderer {
namespace win32 {

using mozc::commands::Information;
using mozc::commands::InformationList;
using mozc::commands::Output;
using mozc::commands::SessionCommand;
using mozc::renderer::RendererStyle;

namespace {
const COLORREF kDefaultBackgroundColor = RGB(0xff, 0xff, 0xff);
const UINT_PTR kIdDelayShowHideTimer = 100;

void FillSolidRect(HDC dc, const RECT* rect, COLORREF color) {
  COLORREF old_color = ::SetBkColor(dc, color);
  if (old_color != CLR_INVALID) {
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    ::SetBkColor(dc, old_color);
  }
}

}  // namespace

// ------------------------------------------------------------------------
// InfolistWindow
// ------------------------------------------------------------------------

InfolistWindow::InfolistWindow()
    : send_command_interface_(nullptr),
      candidate_window_(new commands::CandidateWindow),
      dpi_(::GetDpiForSystem()),
      text_renderer_(TextRenderer::Create(dpi_)),
      style_(new RendererStyle),
      metrics_changed_(false),
      visible_(false) {
  GetScaledRendererStyle(style_.get(), dpi_);
}

InfolistWindow::~InfolistWindow() {}

void InfolistWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == dpi_) {
    return;
  }
  dpi_ = dpi;
  GetScaledRendererStyle(style_.get(), dpi_);
  text_renderer_->OnDpiChanged(dpi_);
}

void InfolistWindow::OnDestroy() {
  // PostQuitMessage may stop the message loop even though other
  // windows are not closed. WindowManager should close these windows
  // before process termination.
  ::PostQuitMessage(0);
}

BOOL InfolistWindow::OnEraseBkgnd(HDC dc) {
  // We do not have to erase background
  // because all pixels in client area will be drawn in the DoPaint method.
  return TRUE;
}

void InfolistWindow::OnGetMinMaxInfo(MINMAXINFO* min_max_info) {
  // Do not restrict the window size in case the candidate window must be
  // very small size.
  min_max_info->ptMinTrackSize.x = 1;
  min_max_info->ptMinTrackSize.y = 1;
  SetMsgHandled(TRUE);
}

void InfolistWindow::OnPaint(HDC dc) {
  CRect client_rect;
  this->GetClientRect(&client_rect);

  wil::unique_hdc_paint paint_dc;
  if (dc == nullptr) {
    paint_dc = wil::BeginPaint(this->m_hWnd);
  }
  HDC target_dc = paint_dc.is_valid() ? paint_dc.get() : dc;

  // Render to off-screen bitmap first to avoid tearing.
  wil::unique_hdc memdc(::CreateCompatibleDC(target_dc));
  wil::unique_hbitmap bitmap(::CreateCompatibleBitmap(
      target_dc, client_rect.Width(), client_rect.Height()));
  wil::unique_select_object old_bitmap =
      wil::SelectObject(memdc.get(), bitmap.get());
  DoPaint(memdc.get());
  ::BitBlt(target_dc, client_rect.left, client_rect.top, client_rect.Width(),
           client_rect.Height(), memdc.get(), 0, 0, SRCCOPY);
}

void InfolistWindow::OnPrintClient(HDC dc, UINT uFlags) { OnPaint(dc); }

// IMi：意味の窓。選んでいる候補の項目1つだけを、辞書らしい形で描く
//   1行目：見出しの語（大きく）、読み・品詞（小さく）
//   区切りの線
//   意味：「1. …」は番号を差し色で、「  ・…」は字下げ、「［動詞］」「表記：…」は小さく
//   ウィクショナリーの項目なら、最後に出典
namespace {

// 「言う（いう）［動詞］」→（言う, いう, 動詞）。読みや品詞がなければ空
void SplitTitle(const std::wstring& t, std::wstring* word, std::wstring* reading,
                std::wstring* pos) {
  const size_t r0 = t.find(L'（'), p0 = t.find(L'［');
  const size_t end = std::min(r0, p0);
  *word = t.substr(0, end == std::wstring::npos ? t.size() : end);
  reading->clear();
  pos->clear();
  if (r0 != std::wstring::npos) {
    const size_t r1 = t.find(L'）', r0);
    if (r1 != std::wstring::npos) *reading = t.substr(r0 + 1, r1 - r0 - 1);
  }
  if (p0 != std::wstring::npos) {
    const size_t p1 = t.find(L'］', p0);
    if (p1 != std::wstring::npos) *pos = t.substr(p0 + 1, p1 - p0 - 1);
  }
}

std::vector<std::wstring> SplitLines(const std::wstring& s) {
  std::vector<std::wstring> out;
  size_t b = 0;
  while (b <= s.size()) {
    const size_t e = s.find(L'\n', b);
    out.push_back(s.substr(b, e == std::wstring::npos ? std::wstring::npos : e - b));
    if (e == std::wstring::npos) break;
    b = e + 1;
  }
  return out;
}

// ウィクショナリーの項目の番号（usage_rewriter.cc の kWiktUsageIdBase）
constexpr int32_t kWiktUsageIdBase = 1 << 24;

}  // namespace

Size InfolistWindow::DoPaint(HDC dc) {
  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  const InformationList& usages = candidate_window_->usages();
  const int width = infostyle.window_width();
  const double scale = GetDPIScalingFactor(dpi_);
  const int pad_x = static_cast<int>(16 * scale), pad_y = static_cast<int>(14 * scale);
  const int inner = width - pad_x * 2;
  const ImiTheme& theme = ImiTheme::Current();
  if (dc != nullptr) {
    ::SetBkMode(dc, TRANSPARENT);
    const CRect all(0, 0, width, 4096);
    FillSolidRect(dc, &all, theme.window_bg);
  }
  if (usages.information_size() == 0) {
    return Size(width, pad_y * 2);
  }
  const int index = usages.has_focused_index() ? usages.focused_index() : 0;
  const Information& info = usages.information(index < usages.information_size() ? index : 0);

  int y = pad_y;
  // 1行目：見出しの語と、読み・品詞
  std::wstring word, reading, pos;
  SplitTitle(mozc::win32::Utf8ToWide(info.title()), &word, &reading, &pos);
  std::wstring sub = reading;
  if (!pos.empty()) sub += (sub.empty() ? L"" : L"・") + pos;
  const Size word_size = text_renderer_->MeasureString(TextRenderer::FONTSET_INFOLIST_TITLE, word);
  const Size sub_size = sub.empty() ? Size(0, 0)
                                    : text_renderer_->MeasureString(
                                          TextRenderer::FONTSET_INFOLIST_CAPTION, sub);
  if (dc != nullptr) {
    text_renderer_->RenderText(dc, word, Rect(pad_x, y, word_size.width, word_size.height),
                               TextRenderer::FONTSET_INFOLIST_TITLE);
    if (!sub.empty()) {
      const int sx = pad_x + word_size.width + static_cast<int>(8 * scale);
      text_renderer_->RenderText(
          dc, sub,
          Rect(sx, y + word_size.height - sub_size.height - static_cast<int>(2 * scale),
               std::max(0, width - pad_x - sx), sub_size.height),
          TextRenderer::FONTSET_INFOLIST_CAPTION);
    }
  }
  y += word_size.height + static_cast<int>(8 * scale);
  // 区切りの線
  if (dc != nullptr) {
    const CRect line(pad_x, y, width - pad_x, y + std::max(1, static_cast<int>(scale)));
    FillSolidRect(dc, &line, theme.separator);
  }
  y += static_cast<int>(8 * scale);

  // 意味
  const int num_w = text_renderer_->MeasureString(TextRenderer::FONTSET_INFOLIST_ACCENT, L"8").width +
                    static_cast<int>(8 * scale);
  const int gap = static_cast<int>(3 * scale);
  for (std::wstring line : SplitLines(mozc::win32::Utf8ToWide(info.description()))) {
    if (line.empty()) continue;
    // 「1. 言葉に出す。」：番号と本文
    size_t dot = line.find(L". ");
    bool numbered = dot != std::wstring::npos && dot > 0 && dot <= 2;
    for (size_t k = 0; numbered && k < dot; ++k) numbered = iswdigit(line[k]);
    if (numbered) {
      const std::wstring num = line.substr(0, dot), body = line.substr(dot + 2);
      const Size bs = text_renderer_->MeasureStringMultiLine(
          TextRenderer::FONTSET_INFOLIST_DESCRIPTION, body, inner - num_w);
      if (dc != nullptr) {
        text_renderer_->RenderText(dc, num, Rect(pad_x, y, num_w, bs.height),
                                   TextRenderer::FONTSET_INFOLIST_ACCENT);
        text_renderer_->RenderText(dc, body, Rect(pad_x + num_w, y, inner - num_w, bs.height),
                                   TextRenderer::FONTSET_INFOLIST_DESCRIPTION);
      }
      y += bs.height + gap;
      continue;
    }
    // 「  ・…」：字下げした細目
    if (line.rfind(L"  ・", 0) == 0) {
      const std::wstring body = line.substr(2);
      const Size bs = text_renderer_->MeasureStringMultiLine(
          TextRenderer::FONTSET_INFOLIST_DESCRIPTION, body, inner - num_w);
      if (dc != nullptr) {
        text_renderer_->RenderText(dc, body, Rect(pad_x + num_w, y, inner - num_w, bs.height),
                                   TextRenderer::FONTSET_INFOLIST_DESCRIPTION);
      }
      y += bs.height + gap;
      continue;
    }
    // 「［動詞］」「表記：…」などは小さく
    const bool is_small = line.rfind(L"［", 0) == 0 || line.rfind(L"表記：", 0) == 0;
    const auto font = is_small ? TextRenderer::FONTSET_INFOLIST_CAPTION
                            : TextRenderer::FONTSET_INFOLIST_DESCRIPTION;
    const Size ls = text_renderer_->MeasureStringMultiLine(font, line, inner);
    if (dc != nullptr) {
      text_renderer_->RenderText(dc, line, Rect(pad_x, y, inner, ls.height), font);
    }
    y += ls.height + gap;
  }
  // 出典
  if (info.id() >= kWiktUsageIdBase) {
    y += static_cast<int>(6 * scale);
    const std::wstring src = L"ウィクショナリー日本語版より";
    const Size ss = text_renderer_->MeasureString(TextRenderer::FONTSET_INFOLIST_CAPTION, src);
    if (dc != nullptr) {
      text_renderer_->RenderText(dc, src, Rect(pad_x, y, inner, ss.height),
                                 TextRenderer::FONTSET_INFOLIST_CAPTION);
    }
    y += ss.height;
  }
  y += pad_y;
  if (dc != nullptr) {
    const CRect frame(0, 0, width, y);
    ::SetDCBrushColor(dc, theme.border);
    ::FrameRect(dc, &frame, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
  }
  return Size(width, y);
}

void InfolistWindow::OnSettingChange(UINT uFlags, LPCTSTR /*lpszSection*/) {
  // Since TextRenderer uses dialog font to render,
  // we monitor font-related parameters to know when the font style is changed.
  switch (uFlags) {
    case 0x1049:  // = SPI_SETCLEARTYPE
    case SPI_SETFONTSMOOTHING:
    case SPI_SETFONTSMOOTHINGCONTRAST:
    case SPI_SETFONTSMOOTHINGORIENTATION:
    case SPI_SETFONTSMOOTHINGTYPE:
    case SPI_SETNONCLIENTMETRICS:
      metrics_changed_ = true;
      break;
    default:
      // We ignore other changes.
      break;
  }
}

void InfolistWindow::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent != kIdDelayShowHideTimer) {
    return;
  }
  if (visible_) {
    DelayShow(0);
  } else {
    DelayHide(0);
  }
}

void InfolistWindow::DelayShow(UINT mseconds) {
  visible_ = true;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SendMessageW(WM_NCACTIVATE, FALSE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::DelayHide(UINT mseconds) {
  visible_ = false;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    ShowWindow(SW_HIDE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::UpdateLayout(
    const commands::CandidateWindow& candidate_window) {
  *candidate_window_ = candidate_window;

  // IMi：スタイルの設定や Windows の配色が変わっていれば、色と字体を作り直す
  ImiTheme::Refresh();
  if (theme_generation_ != ImiTheme::generation()) {
    theme_generation_ = ImiTheme::generation();
    GetScaledRendererStyle(style_.get(), dpi_);
    metrics_changed_ = true;
    if (m_hWnd != nullptr) {
      const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
      ::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
      const COLORREF border = ImiTheme::Current().border;
      ::DwmSetWindowAttribute(m_hWnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    }
  }

  // If we detect any change of font parameters, update text renderer
  if (metrics_changed_) {
    text_renderer_->OnThemeChanged();
    metrics_changed_ = false;
  }
}

void InfolistWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
}

Size InfolistWindow::GetLayoutSize() { return DoPaint(nullptr); }
}  // namespace win32
}  // namespace renderer
}  // namespace mozc
