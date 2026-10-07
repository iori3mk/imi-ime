// IMi：入力モード（IME のオン・オフと変換モード）を、すべてのアプリで共通にする。
//
// Windows の入力モードの値（TSF のコンパートメント）はスレッドごとで、Chrome とターミナルのように
// アプリをまたいでは共有されないことがあった。そこで IMi が利用者ごとのレジストリ
// （HKCU\Software\IMi の SharedOpen・SharedMode）に最後の入力モードを書き、入力欄を選んだときに読んで合わせる。
// 共通にするかは、設定画面が書く HKCU\Software\IMi の ShareInputMode（ないときは共通にする）。

#ifndef MOZC_WIN32_TIP_IMI_SHARED_MODE_H_
#define MOZC_WIN32_TIP_IMI_SHARED_MODE_H_

#include <windows.h>

namespace mozc {
namespace win32 {
namespace tsf {

// 入力モードをすべてのアプリで共通にするか
bool ImiShareInputMode();

// 最後の入力モード。書かれていなければ false
bool ImiReadSharedMode(bool* open, DWORD* native_mode);

// 最後の入力モードを書く（同じ値なら書かない）
void ImiWriteSharedMode(bool open, DWORD native_mode);

}  // namespace tsf
}  // namespace win32
}  // namespace mozc

#endif  // MOZC_WIN32_TIP_IMI_SHARED_MODE_H_
