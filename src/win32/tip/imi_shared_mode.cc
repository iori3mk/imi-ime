#include "win32/tip/imi_shared_mode.h"

#include <windows.h>

#pragma comment(lib, "advapi32.lib")

namespace mozc {
namespace win32 {
namespace tsf {
namespace {

constexpr wchar_t kKey[] = L"Software\\IMi";

bool ReadDword(const wchar_t* name, DWORD* value) {
  DWORD size = sizeof(*value);
  return ::RegGetValueW(HKEY_CURRENT_USER, kKey, name, RRF_RT_REG_DWORD, nullptr, value,
                        &size) == ERROR_SUCCESS;
}

void WriteDword(const wchar_t* name, DWORD value) {
  ::RegSetKeyValueW(HKEY_CURRENT_USER, kKey, name, REG_DWORD, &value, sizeof(value));
}

}  // namespace

bool ImiShareInputMode() {
  DWORD value = 1;
  if (!ReadDword(L"ShareInputMode", &value)) return true;
  return value != 0;
}

bool ImiReadSharedMode(bool* open, DWORD* native_mode) {
  DWORD o = 0, m = 0;
  if (!ReadDword(L"SharedOpen", &o) || !ReadDword(L"SharedMode", &m)) return false;
  *open = o != 0;
  *native_mode = m;
  return true;
}

void ImiWriteSharedMode(bool open, DWORD native_mode) {
  bool cur_open = false;
  DWORD cur_mode = 0;
  if (ImiReadSharedMode(&cur_open, &cur_mode) && cur_open == open && cur_mode == native_mode) {
    return;
  }
  WriteDword(L"SharedOpen", open ? 1 : 0);
  WriteDword(L"SharedMode", native_mode);
}

}  // namespace tsf
}  // namespace win32
}  // namespace mozc
