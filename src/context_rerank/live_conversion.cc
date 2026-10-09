#include "context_rerank/live_conversion.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/file_util.h"
#include "base/system_util.h"

namespace mozc::context_rerank {
namespace {

thread_local int preview_depth = 0;
thread_local int skip_lm_depth = 0;
thread_local int commit_depth = 0;
thread_local bool pending = false;
thread_local int budget_depth = 0;

// rerank_config.txt の name の値（なければ default_value）
std::string ReadConfigValue(absl::string_view name, absl::string_view default_value) {
  std::string value(default_value);
  std::vector<std::pair<std::string, std::string>> entries;
  if (!ReadRerankConfig(&entries)) return value;
  for (const auto& [key, v] : entries) {
    if (key == name) value = v;
  }
  return value;
}

bool ReadLiveConfig() { return ReadConfigValue("live_conversion", "0") == "1"; }

bool ReadStabilizeConfig() {
  const char* env = std::getenv("MOZC_LIVE_STABILIZE");
  if (env != nullptr && *env != '\0') return std::string(env) != "0";
  return ReadConfigValue("live_stabilize", "1") == "1";
}

}  // namespace

std::string AssetDir() {
  const char* dir = std::getenv("MOZC_CONTEXT_RERANK_DIR");
  if (dir != nullptr && *dir != '\0') return dir;
#ifdef _WIN32
  return FileUtil::JoinPath(SystemUtil::GetServerDirectory(), "context_rerank");
#else
  return "";
#endif  // _WIN32
}

bool ReadRerankConfig(std::vector<std::pair<std::string, std::string>>* entries) {
  entries->clear();
  const std::string dir = AssetDir();
  if (dir.empty()) return false;
  std::ifstream cf(dir + "/rerank_config.txt");
  if (!cf) return false;
  std::string line;
  while (std::getline(cf, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();  // メモ帳などで CRLF にしたとき
    std::vector<std::string> f = absl::StrSplit(line, ' ', absl::SkipEmpty());
    if (f.size() != 2 || f[0][0] == '#') continue;
    entries->emplace_back(std::move(f[0]), std::move(f[1]));
  }
  return true;
}

bool LiveConversionEnabled() {
  static const bool enabled = ReadLiveConfig();
  return enabled;
}

bool LiveStabilizeEnabled() {
  static const bool enabled = ReadStabilizeConfig();
  return enabled;
}

int LiveHoldReleaseMs() {
  static const int ms = [] {
    const char* env = std::getenv("MOZC_LIVE_HOLD_MS");
    std::string v = env != nullptr && *env != '\0' ? env : ReadConfigValue("live_hold_release_ms", "200");
    int n = 200;
    return absl::SimpleAtoi(v, &n) ? n : 200;
  }();
  return ms;
}

ScopedLivePreview::ScopedLivePreview() { ++preview_depth; }
ScopedLivePreview::~ScopedLivePreview() { --preview_depth; }

bool InLivePreview() { return preview_depth > 0; }

ScopedSkipLm::ScopedSkipLm() { ++skip_lm_depth; }
ScopedSkipLm::~ScopedSkipLm() { --skip_lm_depth; }

bool SkipLm() { return skip_lm_depth > 0; }

ScopedLiveCommit::ScopedLiveCommit() { ++commit_depth; }
ScopedLiveCommit::~ScopedLiveCommit() { --commit_depth; }

bool InLiveCommit() { return commit_depth > 0; }

ScopedLmBudget::ScopedLmBudget() { ++budget_depth; }
ScopedLmBudget::~ScopedLmBudget() { --budget_depth; }
bool InLmBudget() { return budget_depth > 0; }

void SetLivePending() { pending = true; }

bool TakeLivePending() {
  const bool p = pending;
  pending = false;
  return p;
}

}  // namespace mozc::context_rerank
