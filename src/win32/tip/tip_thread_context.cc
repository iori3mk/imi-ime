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

#include "win32/tip/tip_thread_context.h"

#include <windows.h>

#include <cstdint>
#include <limits>

#include "base/win32/win_util.h"
#include "win32/tip/imi_shared_mode.h"
#include "win32/tip/tip_input_mode_manager.h"

namespace mozc {
namespace win32 {
namespace tsf {

namespace {

TipInputModeManager::Config GetConfig() {
  TipInputModeManager::Config config;
  config.use_global_mode = WinUtil::IsPerUserInputSettingsEnabled();
  // 入力モードも共通にするときは、use_global_mode のもとでも Windows の共有の変換モードを読む
  // （元の Mozc は use_global_mode のとき変換モードをスレッドごとに持ち、Windows の値を読まなかった）
  config.share_conversion_mode = ImiShareInputMode();
  return config;
}

}  // namespace

TipThreadContext::TipThreadContext() : input_mode_manager_(GetConfig()) {}

void TipThreadContext::IncrementFocusRevision() {
  if (focus_revision_ < std::numeric_limits<int32_t>::max()) {
    focus_revision_++;
  } else {
    focus_revision_ = 0;
  }
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
