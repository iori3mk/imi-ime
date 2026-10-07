#include "context_rerank/wikt_dict.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/mmap.h"
#include "context_rerank/live_conversion.h"

namespace mozc::context_rerank {
namespace {
constexpr char kMagic[] = "IMIWKT01";
constexpr size_t kHeader = 8 + 4;
}  // namespace

const WiktDict& WiktDict::Get() {
  static const WiktDict* dict = new WiktDict();
  return *dict;
}

WiktDict::WiktDict() {
  const std::string path = FileUtil::JoinPath(AssetDir(), "wikt_dict.bin");
  if (!FileUtil::FileExists(path).ok()) return;
  absl::StatusOr<Mmap> m = Mmap::Map(path, Mmap::READ_ONLY);
  if (!m.ok() || m->size() < kHeader || std::memcmp(m->data(), kMagic, 8) != 0) return;
  mmap_ = std::make_unique<Mmap>(*std::move(m));
  uint32_t n;
  std::memcpy(&n, mmap_->data() + 8, 4);
  const size_t recs_bytes = static_cast<size_t>(n) * 6 * 4;
  if (mmap_->size() < kHeader + recs_bytes) {
    mmap_.reset();
    return;
  }
  n_ = n;
  recs_ = reinterpret_cast<const uint32_t*>(mmap_->data() + kHeader);
  blob_ = mmap_->data() + kHeader + recs_bytes;
  blob_size_ = mmap_->size() - kHeader - recs_bytes;
}

absl::string_view WiktDict::Str(uint32_t pos, uint32_t len) const {
  if (static_cast<size_t>(pos) + len > blob_size_) return {};
  return absl::string_view(blob_ + pos, len);
}

int32_t WiktDict::Find(absl::string_view key) const {
  if (n_ == 0 || key.empty()) return -1;
  // 鍵は UTF-8 のバイト順に並んでいるので、二分探索で引く
  uint32_t lo = 0, hi = n_;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    const absl::string_view k = Str(recs_[mid * 6], recs_[mid * 6 + 1]);
    if (k < key) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < n_ && Str(recs_[lo * 6], recs_[lo * 6 + 1]) == key) {
    return static_cast<int32_t>(lo);
  }
  return -1;
}

bool WiktDict::Lookup(absl::string_view key, absl::string_view* title,
                      absl::string_view* description) const {
  const int32_t i = Find(key);
  if (i < 0) return false;
  *title = Str(recs_[i * 6 + 2], recs_[i * 6 + 3]);
  *description = Str(recs_[i * 6 + 4], recs_[i * 6 + 5]);
  return !title->empty();
}

}  // namespace mozc::context_rerank
