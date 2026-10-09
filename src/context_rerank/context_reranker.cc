#include "context_rerank/context_reranker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "context_rerank/mkey.h"

namespace mozc::context_rerank {
namespace {

constexpr double kCostScale = 500.0;  // Mozc のコスト尺度：cost = −log(確率)×500

template <typename F>
bool ReadTsv(const std::string& path, size_t cols, F f) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    std::vector<absl::string_view> x = absl::StrSplit(line, '\t');
    if (x.size() == cols) f(x);
  }
  return true;
}

void Grow(std::vector<double>* v, uint32_t id) {
  if (v->size() <= id) v->resize(id + 1, 0.0);
}

}  // namespace

std::vector<double> MonotoneCosts(const std::vector<int32_t>& costs) {
  std::vector<double> out;
  for (int32_t c : costs) {
    out.push_back(out.empty() ? c : std::max<double>(c, out.back()));
  }
  return out;
}

uint32_t ContextReranker::Intern(absl::string_view key) {
  auto [it, inserted] = ids_.try_emplace(std::string(key), static_cast<uint32_t>(ids_.size()));
  return it->second;
}

uint32_t ContextReranker::Id(absl::string_view key) const {
  if (key.empty()) return kNone;
  if (!view_ids_.empty()) {
    auto it = view_ids_.find(key);
    return it == view_ids_.end() ? kNone : it->second;
  }
  auto it = ids_.find(key);
  return it == ids_.end() ? kNone : it->second;
}

bool ContextReranker::Load(const std::string& dir) {
  if (std::ifstream(dir + "/tables.bin").good()) return LoadBinary(dir + "/tables.bin");
  return LoadTsv(dir);
}

bool ContextReranker::LoadBinary(const std::string& path) {
  // ファイルを写像して、共起・隣の表はコピーせずに引く（読み込みの最中にファイル全体を2重に持たない。IMi S4）
  absl::StatusOr<Mmap> m = Mmap::Map(path, Mmap::READ_ONLY);
  if (!m.ok() || m->size() < 24 || std::memcmp(m->data(), "CTXRR002", 8) != 0) return false;
  mmap_ = std::make_unique<Mmap>(*std::move(m));
  const absl::string_view buf(mmap_->data(), mmap_->size());
  // 途中で切れた・壊れたファイルで範囲の外を読まないよう、読むたびに残りの大きさを確かめる。
  // 足りなければ nullptr を返し、読み込みを失敗にする（表なしの Mozc と同じ動きに戻る）
  size_t pos = 8;
  auto take = [&](size_t bytes) -> const char* {
    if (bytes > buf.size() - pos) return nullptr;
    const char* p = buf.data() + pos;
    pos += bytes;
    return p;
  };
  auto fail = [&] {
    mmap_.reset();
    view_ids_.clear();
    return false;
  };
  uint32_t head[4];
  const char* h = take(16);
  if (h == nullptr) return fail();
  std::memcpy(head, h, 16);
  const uint32_t K = head[0];
  // 鍵の数ごとに少なくとも 4（鍵の終わり）+ 8×5（数）+ 1（止め語）+ 4×3（行の始まり）バイトある
  if (K > (buf.size() - pos) / 57) return fail();
  std::vector<uint32_t> ends(K);
  const char* e = take(4 * static_cast<size_t>(K));
  if (e == nullptr) return fail();
  std::memcpy(ends.data(), e, 4 * static_cast<size_t>(K));
  for (uint32_t i = 1; i < K; ++i) {
    if (ends[i] < ends[i - 1]) return fail();
  }
  const char* blob = take(K ? ends[K - 1] : 0);
  if (blob == nullptr) return fail();
  for (uint32_t i = 0; i < K; ++i) {
    const uint32_t b = i ? ends[i - 1] : 0;
    view_ids_.emplace(absl::string_view(blob + b, ends[i] - b), i);
  }
  auto doubles = [&](std::vector<double>* v) {
    const char* p = take(8 * static_cast<size_t>(K));
    if (p == nullptr) return false;
    v->resize(K);
    std::memcpy(v->data(), p, 8 * static_cast<size_t>(K));
    return true;
  };
  std::vector<double> bg;
  if (!doubles(&n_t_) || !doubles(&bg)) return fail();
  const char* stop = take(K);
  if (stop == nullptr || !doubles(&n_target_) || !doubles(&n_left_) || !doubles(&n_right_)) {
    return fail();
  }
  // 数は有限で、n_t_（負は行なし）のほかは負にならず、背景の数の合計は正（点数が NaN になって
  // 選び直しが黙って効かなくなるのを防ぐ）
  double total_bg = 0;
  for (uint32_t i = 0; i < K; ++i) {
    if (!std::isfinite(n_t_[i]) || !std::isfinite(bg[i]) || bg[i] < 0 ||
        !std::isfinite(n_target_[i]) || n_target_[i] < 0 || !std::isfinite(n_left_[i]) ||
        n_left_[i] < 0 || !std::isfinite(n_right_[i]) || n_right_[i] < 0) {
      return fail();
    }
    total_bg += bg[i];
  }
  if (K > 0 && !(total_bg > 0 && std::isfinite(total_bg))) return fail();
  has_row_.assign(K, false);
  for (uint32_t i = 0; i < K; ++i) {
    if (n_t_[i] >= 0) {
      has_row_[i] = true;
    } else {
      n_t_[i] = 0;
    }
  }
  log_pb_.assign(K, std::nan(""));
  stop_.assign(K, false);
  for (uint32_t i = 0; i < K; ++i) {
    if (bg[i] > 0) log_pb_[i] = std::log(bg[i] / total_bg);
    stop_[i] = stop[i] != 0;
  }
  total_target_ = 0;
  for (double x : n_target_) total_target_ += x;
  for (int t = 0; t < 3; ++t) {
    auto* tab = t == 0 ? &pairs_ : t == 1 ? &left_ : &right_;
    const char* off = take(4 * (static_cast<size_t>(K) + 1));
    const char* flat = off == nullptr ? nullptr : take(8 * static_cast<size_t>(head[1 + t]));
    if (flat == nullptr) return fail();
    // 行の始まりは 0 から増えていく一方で、最後が組の数と同じ（Lookup が flat の外を読まない）
    uint32_t prev = 0;
    for (uint32_t i = 0; i <= K; ++i) {
      uint32_t o;
      std::memcpy(&o, off + 4 * static_cast<size_t>(i), 4);
      if ((i == 0 && o != 0) || o < prev) return fail();
      prev = o;
    }
    if (prev != head[1 + t]) return fail();
    tab->off = off;
    tab->flat = flat;
    tab->n = K;
    num_pairs_ += head[1 + t];
  }
  if (pos != buf.size()) return fail();
  return true;
}

bool ContextReranker::LoadTsv(const std::string& dir) {
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>>* tab = nullptr;  // Rows::owned
  uint32_t count;
  auto add_pair = [&](absl::Span<const absl::string_view> x) {
    if (!absl::SimpleAtoi(x[2], &count)) return;
    const uint32_t t = Intern(x[0]), w = Intern(x[1]);
    if (tab->size() <= t) tab->resize(t + 1);
    (*tab)[t].push_back({w, count});
  };
  // T0
  std::vector<double> bg;
  double total_bg = 0;
  tab = &pairs_.owned;
  if (!ReadTsv(dir + "/cooc_pairs.tsv", 3, add_pair)) return false;
  if (!ReadTsv(dir + "/cooc_targets.tsv", 2, [&](auto x) {
        double n;
        if (!absl::SimpleAtod(x[1], &n)) return;
        const uint32_t t = Intern(x[0]);
        Grow(&n_t_, t);
        n_t_[t] = n;
        if (has_row_.size() <= t) has_row_.resize(t + 1, false);
        has_row_[t] = true;
      })) {
    return false;
  }
  if (!ReadTsv(dir + "/cooc_bg.tsv", 2, [&](auto x) {
        double n;
        if (!absl::SimpleAtod(x[1], &n)) return;
        const uint32_t w = Intern(x[0]);
        Grow(&bg, w);
        bg[w] = n;
        total_bg += n;
      })) {
    return false;
  }
  log_pb_.assign(ids_.size(), std::nan(""));
  for (uint32_t w = 0; w < bg.size(); ++w) {
    if (bg[w] > 0) log_pb_[w] = std::log(bg[w] / total_bg);
  }
  stop_.assign(ids_.size(), false);
  ReadTsv(dir + "/cooc_stop.tsv", 1, [&](auto x) {
    const uint32_t w = Id(x[0]);
    if (w != kNone && w < stop_.size()) stop_[w] = true;
  });
  // K5
  tab = &left_.owned;
  if (!ReadTsv(dir + "/adj_left.tsv", 3, add_pair)) return false;
  tab = &right_.owned;
  if (!ReadTsv(dir + "/adj_right.tsv", 3, add_pair)) return false;
  if (!ReadTsv(dir + "/adj_n.tsv", 3, [&](auto x) {
        double n;
        if (!absl::SimpleAtod(x[2], &n)) return;
        const uint32_t k = Intern(x[1]);
        std::vector<double>* v = x[0] == "T" ? &n_target_ : x[0] == "L" ? &n_left_ : &n_right_;
        Grow(v, k);
        (*v)[k] = n;
        if (x[0] == "T") total_target_ += n;
      })) {
    return false;
  }
  for (auto* t : {&pairs_.owned, &left_.owned, &right_.owned}) {
    for (auto& row : *t) {
      std::sort(row.begin(), row.end());
      num_pairs_ += row.size();
    }
  }
  // 後から増えた番号に合わせて長さをそろえる
  const size_t n = ids_.size();
  log_pb_.resize(n, std::nan(""));
  stop_.resize(n, false);
  return true;
}

bool ContextReranker::Usable(uint32_t w) const {
  return w != kNone && w < log_pb_.size() && !std::isnan(log_pb_[w]) && !stop_[w];
}

uint32_t ContextReranker::Rows::Lookup(uint32_t t, uint32_t w) const {
  if (off == nullptr) {
    if (t >= owned.size()) return 0;
    const auto& row = owned[t];
    auto it = std::lower_bound(row.begin(), row.end(), std::make_pair(w, 0u));
    return (it != row.end() && it->first == w) ? it->second : 0;
  }
  if (t >= n) return 0;
  // 写像したファイルの中は4バイト境界とは限らないので memcpy で読む
  auto u32 = [](const char* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
  };
  const uint32_t end = u32(off + 4 * (static_cast<size_t>(t) + 1));
  uint32_t lo = u32(off + 4 * static_cast<size_t>(t)), hi = end;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (u32(flat + 8 * static_cast<size_t>(mid)) < w) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < end && u32(flat + 8 * static_cast<size_t>(lo)) == w) {
    return u32(flat + 8 * static_cast<size_t>(lo) + 4);
  }
  return 0;
}

double ContextReranker::CoocScore(uint32_t t, const std::vector<uint32_t>& words) const {
  if (!InCooc(t) || words.empty()) return 0.0;
  const double n_t = n_t_[t];
  double s = 0;
  for (uint32_t w : words) {
    const double expected = n_t * std::exp(log_pb_[w]);
    s += std::log((pairs_.Lookup(t, w) + a_) / (expected + a_));
  }
  return s;
}

double ContextReranker::AdjScore(uint32_t t, uint32_t left, uint32_t right) const {
  if (!KnownAdj(t)) return 0.0;
  const double p_t = n_target_[t] / total_target_;
  double s = 0;
  if (left != kNone && left < n_left_.size() && n_left_[left] > 0) {
    s += std::log((left_.Lookup(t, left) + a_) / (n_left_[left] * p_t + a_));
  }
  if (right != kNone && right < n_right_.size() && n_right_[right] > 0) {
    s += std::log((right_.Lookup(t, right) + a_) / (n_right_[right] * p_t + a_));
  }
  return s;
}

std::vector<int> ContextReranker::Rerank(const std::vector<SegmentInput>& segs,
                                         const std::vector<std::string>& ctx_keys,
                                         double lam, double lam2, int passes) const {
  const size_t n = segs.size();
  std::vector<std::vector<double>> base(n);
  std::vector<std::vector<uint32_t>> heads(n);
  for (size_t i = 0; i < n; ++i) {
    base[i] = MonotoneCosts(segs[i].costs);
    for (const std::string& v : segs[i].values) {
      heads[i].push_back(Id(MakeKey(v, segs[i].reading)));
    }
  }
  std::vector<uint32_t> left_words;
  for (const std::string& k : ctx_keys) {
    const uint32_t w = Id(k);
    if (Usable(w)) left_words.push_back(w);
  }
  const uint32_t left_last = ctx_keys.empty() ? kNone : Id(ctx_keys.back());
  std::vector<int> choice(n, 0);
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<uint32_t> cur(n);
    for (size_t i = 0; i < n; ++i) cur[i] = heads[i][choice[i]];
    std::vector<int> next(n, 0);
    for (size_t i = 0; i < n; ++i) {
      std::vector<uint32_t> words = left_words;
      for (size_t j = 0; j < n; ++j) {
        if (j != i && Usable(cur[j])) words.push_back(cur[j]);
      }
      const uint32_t h0 = heads[i][0];
      const bool has_t0 = InCooc(h0);
      const double t0_0 = has_t0 ? CoocScore(h0, words) : 0;
      bool has_adj = false;
      double adj_0 = 0;
      uint32_t left_n = kNone, right_n = kNone;
      if (lam2 != 0) {
        left_n = i > 0 ? cur[i - 1] : left_last;
        right_n = i + 1 < n ? cur[i + 1] : kNone;
        has_adj = KnownAdj(h0);
        if (has_adj) adj_0 = AdjScore(h0, left_n, right_n);
      }
      int best_k = 0;
      double best_margin = 0;
      for (size_t k = 1; k < segs[i].values.size(); ++k) {
        const uint32_t hk = heads[i][k];
        const bool use_t0 = has_t0 && InCooc(hk);
        const bool use_adj = has_adj && KnownAdj(hk) && hk != h0;
        if (!use_t0 && !use_adj) continue;
        double margin = -(base[i][k] - base[i][0]) / kCostScale;
        if (use_adj) margin += lam2 * (AdjScore(hk, left_n, right_n) - adj_0);
        if (use_t0) margin += lam * (CoocScore(hk, words) - t0_0);
        if (margin > best_margin) {
          best_k = static_cast<int>(k);
          best_margin = margin;
        }
      }
      next[i] = best_k;
    }
    if (next == choice) break;
    choice = next;
  }
  return choice;
}

}  // namespace mozc::context_rerank
