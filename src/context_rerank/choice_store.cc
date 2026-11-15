#include "context_rerank/choice_store.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "storage/lru_storage.h"

namespace mozc::context_rerank {
namespace {

constexpr uint32_t kValueSize = 4;
constexpr uint32_t kLruSize = 10000;
constexpr uint32_t kSeedValue = 0x494d6943;

// 組の2語を文字の順に並べたとき、どちらを選んだか
constexpr uint32_t kFirst = 1;
constexpr uint32_t kSecond = 2;
constexpr uint32_t kBoth = 3;  // 両方を使う（文脈で選ぶ）

// 組の鍵（2語は文字の順）。a が先なら *a_first が true
std::string PairKey(absl::string_view reading, absl::string_view a, absl::string_view b,
                    bool* a_first) {
  *a_first = a < b;
  return *a_first ? absl::StrCat(reading, "\t", a, "\t", b) : absl::StrCat(reading, "\t", b, "\t", a);
}

}  // namespace

ChoiceStore::ChoiceStore(const std::string& filename)
    : storage_(std::make_unique<storage::LruStorage>()) {
  if (!storage_->OpenOrCreate(filename.c_str(), kValueSize, kLruSize, kSeedValue)) {
    storage_.reset();
  }
}

void ChoiceStore::Learn(absl::string_view reading, absl::string_view chosen,
                        absl::string_view rejected) {
  if (!storage_ || reading.empty() || chosen.empty() || rejected.empty() || chosen == rejected) {
    return;
  }
  bool chosen_first;
  const std::string key = PairKey(reading, chosen, rejected, &chosen_first);
  uint32_t value = chosen_first ? kFirst : kSecond;
  if (const char* p = storage_->Lookup(key); p != nullptr) {
    uint32_t old;
    std::memcpy(&old, p, sizeof(old));
    if (old != value) value = kBoth;  // 逆向きにも選んだ（kBoth はそのまま）
  }
  storage_->Insert(key, reinterpret_cast<const char*>(&value));
}

bool ChoiceStore::Prefers(absl::string_view reading, absl::string_view winner,
                          absl::string_view loser) const {
  if (!storage_ || reading.empty() || winner.empty() || loser.empty() || winner == loser) {
    return false;
  }
  bool winner_first;
  const std::string key = PairKey(reading, winner, loser, &winner_first);
  const char* p = storage_->Lookup(key);
  if (p == nullptr) return false;
  uint32_t value;
  std::memcpy(&value, p, sizeof(value));
  return value == (winner_first ? kFirst : kSecond);
}

void ChoiceStore::Clear() {
  if (storage_) storage_->Clear();
}

}  // namespace mozc::context_rerank
