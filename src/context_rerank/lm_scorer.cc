#include "context_rerank/lm_scorer.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "onnxruntime_cxx_api.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // std::min・std::max と衝突させない
#endif  // NOMINMAX
#include <windows.h>

#include "base/win32/wide_char.h"
#endif  // _WIN32

namespace mozc::context_rerank {
namespace {

constexpr size_t kMaxLen = 512;

// ONNX Runtime のモデルのパス（Windows ではワイド文字）
std::basic_string<ORTCHAR_T> OrtPath(const std::string& utf8) {
#ifdef _WIN32
  return win32::Utf8ToWide(utf8);
#else
  return utf8;
#endif  // _WIN32
}

bool ReadFloats(const std::string& path, std::vector<float>* out) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  const std::streamsize size = f.tellg();
  f.seekg(0);
  out->resize(size / sizeof(float));
  return static_cast<bool>(f.read(reinterpret_cast<char*>(out->data()), size));
}

size_t Lcp(const std::vector<int>& a, const std::vector<int>& b) {
  size_t i = 0;
  while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
  return i;
}

// a[lo:] と b[lo:] の末尾の共通トークン数
size_t LcsTail(const std::vector<int>& a, const std::vector<int>& b, size_t lo) {
  size_t i = 0;
  while (i < a.size() - lo && i < b.size() - lo &&
         a[a.size() - 1 - i] == b[b.size() - 1 - i]) {
    ++i;
  }
  return i;
}

}  // namespace

LmScorer::LmScorer() = default;
LmScorer::~LmScorer() = default;

bool LmScorer::Load(const std::string& assets, int threads,
                    const std::string& precision, bool use_arena) {
  if (!sp_.Load(assets + "/pieces.tsv", assets + "/norm.tsv", 0, 2)) {
    return false;
  }
  if (!ReadFloats(assets + "/bos_kv.f32", &bos_kv_) ||
      !ReadFloats(assets + "/bos_lp.f32", &bos_lp_)) {
    return false;
  }
#ifdef _WIN32
  // onnxruntime.dll は遅延読み込み（BUILD.onnxruntime.bazel）。Windows に元からある同名の DLL ではなく、
  // 実行ファイルと同じ場所のものを先に読む（依存するシステムの DLL は System32 から）。
  // なければ B を使わない（IME 自体は動く）
  if (::LoadLibraryExW(L"onnxruntime.dll", nullptr,
                       LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32) == nullptr) {
    std::cerr << "context_rerank: onnxruntime.dll を読めません（エラー " << ::GetLastError() << "）"
              << std::endl;
    return false;
  }
#endif  // _WIN32
  env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "context_rerank");
  Ort::SessionOptions opts;
  opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  if (threads > 0) opts.SetIntraOpNumThreads(threads);
  opts.SetInterOpNumThreads(1);
  // 作業用のメモリの溜め置き（arena）を切る（IMi S4、2026-10-07）。converter_main で作業セット 312→209MB、
  // 専用 267→140MB、B の中央値 28.6→31.4ms（i9、スレッド2）。計算の結果は変わらない
  if (!use_arena) opts.DisableCpuMemArena();
  // 試験用（S4 のメモリの調査）：環境変数 MOZC_IMI_ORT_OPTS に noarena・nopattern・noprepack を含めると切り替える
  if (const char* o = std::getenv("MOZC_IMI_ORT_OPTS"); o != nullptr) {
    const std::string os(o);
    if (os.find("noarena") != std::string::npos) opts.DisableCpuMemArena();
    if (os.find("nopattern") != std::string::npos) opts.DisableMemPattern();
    if (os.find("noprepack") != std::string::npos) opts.AddConfigEntry("session.disable_prepacking", "1");
  }
  prepacked_ = std::make_unique<Ort::PrepackedWeightsContainer>();
  basep_ = std::make_unique<Ort::Session>(
      *env_, OrtPath(assets + "/basep_" + precision + ".onnx").c_str(), opts, *prepacked_);
  alt_ = std::make_unique<Ort::Session>(
      *env_, OrtPath(assets + "/alt_" + precision + ".onnx").c_str(), opts, *prepacked_);
  // IMi：モデルの大きさ（層・頭・1頭の幅）を、文頭の状態の入力（層×2×1×頭×長さ×幅）の形から読む。
  // xsmall（6・8・64）のほかに small（12・12・64）なども使えるように（2026-10-07）
  for (size_t i = 0; i < basep_->GetInputCount(); ++i) {
    const std::vector<int64_t> shape =
        basep_->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() == 6 && shape[0] > 0 && shape[3] > 0 && shape[5] > 0) {
      n_layer_ = static_cast<int>(shape[0]);
      n_head_ = static_cast<int>(shape[3]);
      head_dim_ = static_cast<int>(shape[5]);
      break;
    }
  }
  if (bos_kv_.size() != static_cast<size_t>(n_layer_) * 2 * n_head_ * head_dim_) {
    std::cerr << "context_rerank: bos_kv.f32 の大きさがモデルと合いません" << std::endl;
    return false;
  }
  return true;
}

std::vector<AltScores> LmScorer::Score(absl::string_view prefix,
                                       absl::string_view base,
                                       const std::vector<AltSet>& alts,
                                       int window) const {
  std::vector<int> pre = {bos_id_};
  if (!prefix.empty()) {
    const std::vector<int> p = sp_.Encode(prefix);
    pre.insert(pre.end(), p.begin(), p.end());
  }
  std::vector<int> seq0 = pre;
  {
    const std::vector<int> b = sp_.Encode(base);
    seq0.insert(seq0.end(), b.begin(), b.end());
  }
  if (seq0.size() > kMaxLen) {  // 長すぎるときは前の文脈の左を削る
    const size_t cut = seq0.size() - kMaxLen;
    // 入力中の文だけで長さを超えるときは、前の文脈を削っても収まらないので選び直さない
    if (cut + 1 > pre.size()) return {};
    pre.erase(pre.begin() + 1, pre.begin() + 1 + cut);
    seq0.erase(seq0.begin() + 1, seq0.begin() + 1 + cut);
  }
  const size_t start = pre.size();

  struct Plan {
    size_t c;
    std::vector<std::pair<int, std::vector<int>>> ids;  // （候補の番号, トークン列）
    std::vector<size_t> ends;
  };
  std::vector<Plan> plans;
  for (const AltSet& a : alts) {
    Plan pl;
    for (const auto& [j, text] : a.texts) {
      if (j == 0) {
        pl.ids.push_back({0, seq0});
        continue;
      }
      std::vector<int> v = pre;
      std::vector<int> t = sp_.Encode(text);
      if (pre.size() + t.size() > kMaxLen) t.resize(kMaxLen - pre.size());
      v.insert(v.end(), t.begin(), t.end());
      pl.ids.push_back({j, std::move(v)});
    }
    size_t lcp = SIZE_MAX, min_len = SIZE_MAX;
    for (const auto& [j, v] : pl.ids) {
      lcp = std::min(lcp, Lcp(v, seq0));
      min_len = std::min(min_len, v.size());
    }
    size_t c = std::max(start, lcp);
    c = std::min(c, min_len - 1);
    size_t tail = SIZE_MAX;
    for (const auto& [j, v] : pl.ids) tail = std::min(tail, LcsTail(v, seq0, c));
    for (const auto& [j, v] : pl.ids) {
      pl.ends.push_back(window < 0 ? v.size()
                                   : std::min(v.size(), v.size() - tail + window));
    }
    pl.c = c;
    plans.push_back(std::move(pl));
  }

  Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  // 1. 現在の文：点数に使う位置（予測する側 p = c-1 〜 end-2）の対数確率と KV
  std::set<size_t> need;
  for (const Plan& pl : plans) {
    size_t end0 = 0;
    for (size_t x = 0; x < pl.ids.size(); ++x) {
      if (pl.ids[x].first == 0) end0 = pl.ends[x];
    }
    for (size_t p = pl.c - 1; p + 1 < end0; ++p) need.insert(p);
  }
  std::vector<int64_t> in_ids(seq0.begin() + 1, seq0.end());
  std::vector<int64_t> head_pos, targets;
  for (size_t p : need) {
    if (p == 0) continue;
    head_pos.push_back(static_cast<int64_t>(p - 1));
    targets.push_back(seq0[p + 1]);
  }
  const int64_t T0 = static_cast<int64_t>(in_ids.size());
  const std::array<int64_t, 2> ids_shape = {1, T0};
  const std::array<int64_t, 1> hp_shape = {static_cast<int64_t>(head_pos.size())};
  const std::array<int64_t, 2> tg_shape = {1, static_cast<int64_t>(targets.size())};
  const std::array<int64_t, 6> past_shape = {n_layer_, 2, 1, n_head_, 1, head_dim_};
  std::vector<Ort::Value> inputs;
  inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, in_ids.data(), in_ids.size(), ids_shape.data(), 2));
  inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, head_pos.data(), head_pos.size(), hp_shape.data(), 1));
  inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, targets.data(), targets.size(), tg_shape.data(), 2));
  inputs.push_back(Ort::Value::CreateTensor<float>(mem, const_cast<float*>(bos_kv_.data()), bos_kv_.size(), past_shape.data(), 6));
  const char* in_names[] = {"input_ids", "head_pos", "targets", "past"};
  const char* out_names[] = {"tok_lp", "present"};
  const auto tb0 = std::chrono::steady_clock::now();
  std::vector<Ort::Value> outs = basep_->Run(Ort::RunOptions{nullptr}, in_names, inputs.data(), 4, out_names, 2);
  const double basep_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb0).count();
  static const bool detail = std::getenv("MOZC_CONTEXT_RERANK_TIMING") != nullptr;
  const float* tok_r = outs[0].GetTensorData<float>();
  const float* present = outs[1].GetTensorData<float>();
  const int64_t L0 = T0 + 1;  // present の長さ（文頭を含む）
  absl::flat_hash_map<size_t, float> vals;
  {
    size_t k = 0;
    for (size_t p : need) {
      vals[p] = p == 0 ? bos_lp_[seq0[1]] : tok_r[k++];
    }
  }

  std::vector<AltScores> res;
  struct Row {
    size_t r;      // res の番号
    size_t x;      // scores の番号
    size_t s;      // 入力の開始位置（= c-1）
    std::vector<int64_t> in, tg;
  };
  std::vector<Row> rows;
  for (const Plan& pl : plans) {
    AltScores as;
    as.segment = alts[res.size()].segment;
    for (size_t x = 0; x < pl.ids.size(); ++x) {
      const auto& [j, v] = pl.ids[x];
      if (j == 0) {
        float sum = 0;
        for (size_t p = pl.c - 1; p + 1 < pl.ends[x]; ++p) sum += vals[p];
        as.scores.push_back({0, sum});
        continue;
      }
      as.scores.push_back({j, 0.0f});
      if (pl.ends[x] > pl.c) {  // 入れ替え箇所の直前のトークンから入力し、c 以降を予測させる
        Row row{res.size(), as.scores.size() - 1, pl.c - 1, {}, {}};
        row.in.assign(v.begin() + pl.c - 1, v.begin() + pl.ends[x] - 1);
        row.tg.assign(v.begin() + pl.c, v.begin() + pl.ends[x]);
        rows.push_back(std::move(row));
      }
    }
    res.push_back(std::move(as));
  }
  if (rows.empty()) {
    if (detail) std::cerr << "lm_detail	" << start << "	" << T0 << "	" << basep_ms << "	0	0	0	0" << std::endl;
    return res;
  }

  // 2. 候補を入れ替えた文：1回の計算にまとめる
  size_t P = 0, T = 0;
  for (const Row& r : rows) {
    P = std::max(P, r.s);
    T = std::max(T, r.in.size());
  }
  const size_t B = rows.size();
  std::vector<int64_t> a_in(B * T, 0), a_pos(B * T, 0), a_att(B * (P + T), 0), a_tg(B * T, 0);
  for (size_t b = 0; b < B; ++b) {
    const Row& r = rows[b];
    for (size_t t = 0; t < T; ++t) a_pos[b * T + t] = static_cast<int64_t>(r.s + t);
    for (size_t t = 0; t < r.in.size(); ++t) {
      a_in[b * T + t] = r.in[t];
      a_tg[b * T + t] = r.tg[t];
      a_att[b * (P + T) + P + t] = 1;
    }
    for (size_t t = 0; t < r.s; ++t) a_att[b * (P + T) + t] = 1;
  }
  // present（層, 2, 1, 頭, L0, 次元）の先頭 P 位置を写す
  const size_t D = head_dim_;
  std::vector<float> past(static_cast<size_t>(n_layer_) * 2 * n_head_ * P * D);
  for (int64_t lh = 0; lh < n_layer_ * 2 * n_head_; ++lh) {
    std::copy(present + lh * L0 * D, present + lh * L0 * D + P * D, past.begin() + lh * P * D);
  }
  const std::array<int64_t, 2> bt = {static_cast<int64_t>(B), static_cast<int64_t>(T)};
  const std::array<int64_t, 2> bpt = {static_cast<int64_t>(B), static_cast<int64_t>(P + T)};
  const std::array<int64_t, 6> ps = {n_layer_, 2, 1, n_head_, static_cast<int64_t>(P), head_dim_};
  std::vector<Ort::Value> a_inputs;
  a_inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, a_in.data(), a_in.size(), bt.data(), 2));
  a_inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, a_pos.data(), a_pos.size(), bt.data(), 2));
  a_inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, a_att.data(), a_att.size(), bpt.data(), 2));
  a_inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, a_tg.data(), a_tg.size(), bt.data(), 2));
  a_inputs.push_back(Ort::Value::CreateTensor<float>(mem, past.data(), past.size(), ps.data(), 6));
  const char* a_names[] = {"input_ids", "position_ids", "attention_mask", "targets", "past"};
  const char* a_out[] = {"tok_lp"};
  const auto ta0 = std::chrono::steady_clock::now();
  std::vector<Ort::Value> a_outs = alt_->Run(Ort::RunOptions{nullptr}, a_names, a_inputs.data(), 5, a_out, 1);
  const float* lp = a_outs[0].GetTensorData<float>();
  // 試験用（MOZC_CONTEXT_RERANK_TIMING）：前の文脈のトークン数、現在の文の計算の長さと時間、候補の行数・長さ・流用の長さと時間
  if (detail) {
    std::cerr << "lm_detail	" << start << "	" << T0 << "	" << basep_ms << "	" << B << "	" << T << "	" << P << "	"
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ta0).count() << std::endl;
  }
  for (size_t b = 0; b < B; ++b) {
    float sum = 0;
    for (size_t t = 0; t < rows[b].in.size(); ++t) sum += lp[b * T + t];
    res[rows[b].r].scores[rows[b].x].second = sum;
  }
  return res;
}

}  // namespace mozc::context_rerank
