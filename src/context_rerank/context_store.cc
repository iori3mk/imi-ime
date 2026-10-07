#include "context_rerank/context_store.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "base/util.h"
#include "context_rerank/mkey.h"

namespace mozc::context_rerank {
namespace {
constexpr size_t kMaxKeys = 64;
constexpr size_t kMaxChars = 200;

bool IsSpace(char32_t c) {
  return c == U' ' || c == U'　' || c == U'\t' || c == U'\r' || c == U'\n';
}

std::u32string LastChars(std::u32string s) {
  if (s.size() > kMaxChars) s = s.substr(s.size() - kMaxChars);
  return s;
}
}  // namespace

ContextStore& ContextStore::Get() {
  static ContextStore* store = new ContextStore();
  return *store;
}

void ContextStore::Set(std::vector<std::string> keys, std::string text) {
  absl::MutexLock lock(&mu_);
  segs_.clear();
  for (std::string& k : keys) segs_.push_back({std::move(k), {}});
  text_ = Util::Utf8ToUtf32(text);
  last_commit_ = std::chrono::steady_clock::now();
}

void ContextStore::SetKeys(std::vector<std::string> keys) {
  absl::MutexLock lock(&mu_);
  segs_.clear();
  for (std::string& k : keys) segs_.push_back({std::move(k), {}});
  last_commit_ = std::chrono::steady_clock::now();
}

void ContextStore::SetText(std::string text) {
  absl::MutexLock lock(&mu_);
  text_ = Util::Utf8ToUtf32(text);
  last_commit_ = std::chrono::steady_clock::now();
}

void ContextStore::Commit(const std::vector<std::pair<std::string, std::string>>& segments) {
  absl::MutexLock lock(&mu_);
  for (const auto& [value, reading] : segments) {
    std::u32string v = Util::Utf8ToUtf32(value);
    text_ += v;
    segs_.push_back({MakeKey(value, reading), std::move(v)});
  }
  while (segs_.size() > kMaxKeys) segs_.pop_front();
  text_ = LastChars(std::move(text_));
  last_commit_ = std::chrono::steady_clock::now();
}

void ContextStore::Clear() {
  absl::MutexLock lock(&mu_);
  segs_.clear();
  text_.clear();
}

std::vector<std::string> ContextStore::keys() const {
  absl::MutexLock lock(&mu_);
  std::vector<std::string> keys;
  for (const Seg& s : segs_) keys.push_back(s.key);
  return keys;
}

std::string ContextStore::text() const {
  absl::MutexLock lock(&mu_);
  return Util::Utf32ToUtf8(text_);
}

void ContextStore::Snapshot(bool has_preceding, std::string_view preceding,
                            std::vector<std::string>* keys, std::string* text) {
  absl::MutexLock lock(&mu_);
  keys->clear();
  // 空のときは「新しい入力欄」とは限らない（ターミナルなど、入力中の文字しか見せないアプリは
  // いつも空を返す）ので、受け取れないときと同じに扱う
  if (!has_preceding || preceding.empty()) {
    if (std::chrono::steady_clock::now() - last_commit_ > kIdleClear) {
      segs_.clear();
      text_.clear();
    }
    for (const Seg& s : segs_) keys->push_back(s.key);
    *text = Util::Utf32ToUtf8(text_);
    return;
  }
  const std::u32string p = LastChars(Util::Utf8ToUtf32(preceding));
  *text = Util::Utf32ToUtf8(p);
  // 末尾から、覚えている文節の表記が続けて並んでいる分だけ鍵を使う
  size_t pos = p.size();
  std::vector<std::string> rev;
  for (auto it = segs_.rbegin(); it != segs_.rend(); ++it) {
    while (pos > 0 && IsSpace(p[pos - 1])) --pos;
    const std::u32string& v = it->value;
    if (v.empty() || v.size() > pos || p.compare(pos - v.size(), v.size(), v) != 0) break;
    pos -= v.size();
    rev.push_back(it->key);
  }
  keys->assign(rev.rbegin(), rev.rend());
}

}  // namespace mozc::context_rerank
