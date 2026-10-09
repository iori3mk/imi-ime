#include "context_rerank/context_rerank_rewriter.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "base/file_stream.h"
#include "base/file_util.h"
#include "base/system_util.h"
#include "base/util.h"
#include "context_rerank/context_store.h"
#include "context_rerank/live_conversion.h"
#include "context_rerank/mkey.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "request/conversion_request.h"

namespace mozc::context_rerank {
namespace {

constexpr size_t kScan = 30;  // phase0 の変換ドライバと同じく、上位30件から
constexpr size_t kTop = 10;   // 表記の重複を除いた上位10件を対象にする

std::string LastChars(const std::string& s, int n) {
  const std::u32string u = Util::Utf8ToUtf32(s);
  return u.size() <= static_cast<size_t>(n) ? s : Util::Utf32ToUtf8(u.substr(u.size() - n));
}

// 調べもの用：設定の場所に imi_ctx_debug があるときだけ、前の文脈の扱いを imi_ctx.log に足す。
// あるかは変換エンジンの起動のあと最初に1回だけ確かめる（作ったり消したりしたら変換エンジンを起動し直す）
void DebugLog(absl::string_view what, absl::string_view detail) {
  const std::string dir = SystemUtil::GetUserProfileDirectory();
  static const bool enabled = FileUtil::FileExists(FileUtil::JoinPath(dir, "imi_ctx_debug")).ok();
  if (!enabled) return;
  OutputFileStream f(FileUtil::JoinPath(dir, "imi_ctx.log"), std::ios::app);
  f << std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::system_clock::now().time_since_epoch()).count()
    << "\t" << what << "\t" << detail << "\n";
}

constexpr size_t kCacheSize = 64;  // B の結果を覚えておく件数

// 漢字かカタカナで始まるか、助詞で始まるか（imi-dev の phase0/corpus/ime_ref.py の _particle_alt と同じ）
bool StartsWithKanjiOrKatakana(absl::string_view s) {
  const std::u32string u = Util::Utf8ToUtf32(s);
  if (u.empty()) return false;
  const char32_t c = u[0];
  return (c >= U'一' && c <= U'鿿') || c == U'々' || (c >= U'ァ' && c <= U'ヺ');
}

bool StartsWithParticle(absl::string_view s) {
  for (absl::string_view p : {"から", "まで", "より", "が", "を", "に", "は", "で", "と", "の", "も", "へ", "や"}) {
    if (absl::StartsWith(s, p)) return true;
  }
  return false;
}

// 読みそのものが助詞か
bool IsParticle(absl::string_view s) {
  for (absl::string_view p : {"から", "まで", "より", "が", "を", "に", "は", "で", "と", "の", "も", "へ", "や"}) {
    if (s == p) return true;
  }
  return false;
}

// 前の文脈が語の途中・語の終わりで終わっているか（漢字・仮名・英数字。句読点・空白・記号なら文の始めとみなす）
bool EndsWithWordChar(absl::string_view text) {
  if (text.empty()) return false;
  const std::u32string u = Util::Utf8ToUtf32(text);
  const char32_t c = u.back();
  if (c == U'ー' || c == U'々') return true;
  if ((c >= U'0' && c <= U'9') || (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') ||
      (c >= U'０' && c <= U'９') || (c >= U'Ａ' && c <= U'Ｚ') || (c >= U'ａ' && c <= U'ｚ')) {
    return true;
  }
  const Util::ScriptType t = Util::GetScriptType(Util::CodepointToUtf8(c));
  return t == Util::KANJI || t == Util::HIRAGANA || t == Util::KATAKANA;
}

// B の点数を決める入力（前の文脈・現在の文・入れ替えた文の一覧）を1つの文字列にする
std::string CacheKey(absl::string_view prefix, absl::string_view base,
                     const std::vector<AltSet>& alts) {
  std::string key = std::string(prefix) + '\x1e' + std::string(base);
  for (const AltSet& a : alts) {
    key += '\x1e' + std::to_string(a.segment);
    for (const auto& [p, text] : a.texts) key += '\x1f' + std::to_string(p) + ':' + text;
  }
  return key;
}

}  // namespace

ContextRerankRewriter::ContextRerankRewriter() {
  const std::string dir = AssetDir();
  if (dir.empty()) return;
  // 設定がなければ何もしない（改造前の Mozc と同じ動作）
  std::ifstream cf(dir + "/rerank_config.txt");
  if (!cf) return;
  timing_ = std::getenv("MOZC_CONTEXT_RERANK_TIMING") != nullptr;
  bool use_lm = true;
  int lm_threads = 2;
  bool ort_arena = false;
  std::string precision = "int8";
  std::string line;
  while (std::getline(cf, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();  // メモ帳などで CRLF にしたとき
    std::vector<std::string> f = absl::StrSplit(line, ' ', absl::SkipEmpty());
    if (f.size() != 2 || f[0][0] == '#') continue;
    double v = 0;
    if (f[0] == "precision") {
      precision = f[1];
      continue;
    }
    if (!absl::SimpleAtod(f[1], &v)) continue;
    if (f[0] == "lam") lam_ = v;
    if (f[0] == "lam2") lam2_ = v;
    if (f[0] == "delta") delta_ = v;
    if (f[0] == "window") window_ = static_cast<int>(v);
    if (f[0] == "margin") margin_ = static_cast<int>(v);
    if (f[0] == "max_alt") max_alt_ = static_cast<int>(v);
    if (f[0] == "n_cand") n_cand_ = static_cast<int>(v);
    if (f[0] == "context_chars") context_chars_ = static_cast<int>(v);
    if (f[0] == "lm_budget_ms") lm_budget_ms_ = static_cast<int>(v);
    if (f[0] == "verb_theta") verb_theta_ = v;
    if (f[0] == "lm_light_ms") lm_light_ms_ = v;
    if (f[0] == "light_max_alt") light_max_alt_ = static_cast<int>(v);
    if (f[0] == "light_window") light_window_ = static_cast<int>(v);
    if (f[0] == "use_lm") use_lm = v != 0;
    if (f[0] == "lm_threads") lm_threads = static_cast<int>(v);
    if (f[0] == "ort_arena") ort_arena = v != 0;
  }
  auto reranker = std::make_unique<ContextReranker>();
  if (!reranker->Load(dir)) {
    std::cerr << "context_rerank: 表を読めません: " << dir << std::endl;
    return;
  }
  reranker_ = std::move(reranker);
  {
    std::ifstream vf(dir + "/verb_class.tsv");
    std::string line;
    while (std::getline(vf, line)) {
      const std::vector<std::string> f = absl::StrSplit(line, '\t');
      int c, m;
      if (f.size() == 4 && absl::SimpleAtoi(f[2], &c) && absl::SimpleAtoi(f[3], &m)) {
        verb_class_[f[0] + "\t" + f[1]] = {c, m};
      }
    }
  }
  if (use_lm) {
    auto lm = std::make_unique<LmScorer>();
    if (lm->Load(dir, lm_threads, precision, ort_arena)) {
      lm_ = std::move(lm);
      if (LiveConversionEnabled()) worker_ = std::thread([this] { WorkerLoop(); });
    } else {
      std::cerr << "context_rerank: 小型言語モデルを読めません: " << dir << std::endl;
    }
  }
}

// 直前の文字列が「名詞＋助詞（に・へ・を・と）」で終わるとき、log((話す系+1)/(移動系+1))。
// 名詞は助詞の前の、表にある最も長い末尾（8文字まで）
bool ContextRerankRewriter::VerbRatio(absl::string_view left, double* r) const {
  const std::u32string u = Util::Utf8ToUtf32(left);
  if (u.empty()) return false;
  const char32_t p = u.back();
  if (p != U'に' && p != U'へ' && p != U'を' && p != U'と') return false;
  const std::u32string body = u.substr(0, u.size() - 1);
  for (size_t k = std::min<size_t>(8, body.size()); k > 0; --k) {
    const std::string key = Util::Utf32ToUtf8(body.substr(body.size() - k)) + "\t" +
                            Util::Utf32ToUtf8(std::u32string(1, p));
    const auto it = verb_class_.find(key);
    if (it != verb_class_.end()) {
      *r = std::log((it->second.first + 1.0) / (it->second.second + 1.0));
      return true;
    }
  }
  return false;
}

ContextRerankRewriter::~ContextRerankRewriter() {
  if (worker_.joinable()) {
    {
      absl::MutexLock lock(&mu_);
      stop_ = true;
    }
    worker_.join();
  }
}

bool ContextRerankRewriter::LookupCache(const std::string& key,
                                        std::vector<AltScores>* scores) const {
  absl::MutexLock lock(&mu_);
  for (auto it = cache_.begin(); it != cache_.end(); ++it) {
    if (it->first == key) {
      *scores = it->second;
      cache_.splice(cache_.begin(), cache_, it);  // 最近使ったものを先頭に
      return true;
    }
  }
  return false;
}

void ContextRerankRewriter::StoreCache(const std::string& key,
                                       std::vector<AltScores> scores) const {
  absl::MutexLock lock(&mu_);
  for (const auto& [k, v] : cache_) {
    if (k == key) return;
  }
  cache_.emplace_front(key, std::move(scores));
  if (cache_.size() > kCacheSize) cache_.pop_back();
}

// 遅い PC の見分け：決まった文で B を数回計算し、中央値が lm_light_ms を超えたら軽い設定
// （候補 light_max_alt・後ろの長さ light_window。開発用データで計算量 約6割、精度の差は誤差の範囲）にする。
// 裏のスレッドの最初に1回だけ行う（測っているあいだは通常の設定）
void ContextRerankRewriter::MeasureSpeed() {
  if (lm_light_ms_ <= 0) return;
  const std::string base = "昨日は友達と駅前の喫茶店で会って長い時間話をした";
  std::vector<AltSet> alts(2);
  alts[0].segment = 4;
  alts[1].segment = 7;
  const std::vector<std::pair<std::string, std::string>> rep0 = {
      {"会って", "合って"}, {"会って", "逢って"}, {"会って", "遭って"}, {"会って", "あって"}};
  const std::vector<std::pair<std::string, std::string>> rep1 = {
      {"話を", "放しを"}, {"話を", "離しを"}, {"話を", "はなしを"}, {"話を", "噺を"}};
  alts[0].texts.push_back({0, base});
  alts[1].texts.push_back({0, base});
  for (size_t k = 0; k < rep0.size(); ++k) {
    std::string t = base;
    t.replace(t.find(rep0[k].first), rep0[k].first.size(), rep0[k].second);
    alts[0].texts.push_back({static_cast<int>(k + 1), t});
    std::string u = base;
    u.replace(u.find(rep1[k].first), rep1[k].first.size(), rep1[k].second);
    alts[1].texts.push_back({static_cast<int>(k + 1), u});
  }
  std::vector<double> ms;
  for (int i = 0; i < 6; ++i) {
    const auto s = std::chrono::steady_clock::now();
    lm_->Score("今日はいい天気だった。", base, alts, window_);
    if (i > 0) {  // 最初の1回は読み込みの分が乗るので数えない
      ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count());
    }
  }
  std::sort(ms.begin(), ms.end());
  const double med = ms[ms.size() / 2];
  light_ = med > lm_light_ms_;
  DebugLog("speed", absl::StrCat("median_ms=", med, " light=", light_ ? 1 : 0));
  if (timing_) std::cerr << "lm_speed\t" << med << "\t" << (light_ ? 1 : 0) << std::endl;
}

// 表示用の変換から頼まれた B の計算を、裏で1件ずつ行う。頼まれたものが古くなれば最新の1件だけ残す
void ContextRerankRewriter::WorkerLoop() {
  MeasureSpeed();
  while (true) {
    Job job;
    {
      absl::MutexLock lock(&mu_);
      mu_.Await(absl::Condition(
          +[](ContextRerankRewriter* self) { return self->stop_ || self->next_job_.has_value(); },
          this));
      if (stop_) return;
      job = std::move(*next_job_);
      next_job_.reset();
      running_key_ = job.key;
    }
    std::vector<AltScores> scores = lm_->Score(job.prefix, job.base, job.alts, Window());
    StoreCache(job.key, std::move(scores));
    absl::MutexLock lock(&mu_);
    running_key_.clear();
  }
}

// B の点数。キャッシュになければ、表示用の変換では裏に頼んで false を返し（表示は T0+K5 まで）、
// それ以外（Space による変換、検証用の converter_main）はその場で計算する
bool ContextRerankRewriter::GetLmScores(const std::string& prefix, const std::string& base,
                                        const std::vector<AltSet>& alts,
                                        std::vector<AltScores>* scores) const {
  const std::string key = CacheKey(prefix, base, alts);
  if (LookupCache(key, scores)) return true;
  if (InLivePreview() && worker_.joinable()) {
    absl::MutexLock lock(&mu_);
    if (running_key_ != key) next_job_ = Job{key, prefix, base, alts};
    SetLivePending();
    return false;
  }
  // Space の変換：裏のスレッドがあれば計算を頼み、lm_budget_ms まで待つ。同時変換で同じ文の計算が
  // 裏で進んでいればそれを待つ（二重に計算しない）。間に合わなければ表の結果で候補を出し、
  // SetLivePending() で知らせる（B が終わったら engine_converter が候補を差し替える）。
  // lm_budget_ms が 0 以下なら、今までどおりその場で計算する
  if (InLmBudget() && worker_.joinable() && lm_budget_ms_ > 0) {
    absl::MutexLock lock(&mu_);
    if (running_key_ != key) next_job_ = Job{key, prefix, base, alts};
    struct Arg {
      const ContextRerankRewriter* self;
      const std::string* key;
    } arg{this, &key};
    const bool done = mu_.AwaitWithTimeout(
        absl::Condition(
            +[](Arg* a) ABSL_NO_THREAD_SAFETY_ANALYSIS {
              for (const auto& [k, v] : a->self->cache_) {
                if (k == *a->key) return true;
              }
              return a->self->stop_;
            },
            &arg),
        absl::Milliseconds(lm_budget_ms_));
    if (done) {
      for (auto it = cache_.begin(); it != cache_.end(); ++it) {
        if (it->first == key) {
          *scores = it->second;
          return true;
        }
      }
    }
    SetLivePending();
    return false;
  }
  *scores = lm_->Score(prefix, base, alts, Window());
  StoreCache(key, *scores);
  return true;
}

bool ContextRerankRewriter::Rewrite(const ConversionRequest& request, Segments* segments) const {
  if (!reranker_) return false;
  const auto t0 = std::chrono::steady_clock::now();
  const size_t n = segments->conversion_segments_size();
  if (n == 0) return false;
  std::vector<std::vector<int>> idx(n);  // 対象にした候補の、Mozc の候補の番号
  std::vector<SegmentInput> inputs(n);
  for (size_t i = 0; i < n; ++i) {
    const Segment& s = segments->conversion_segment(i);
    inputs[i].reading = std::string(s.key());
    absl::flat_hash_set<std::string> seen;
    for (size_t c = 0; c < s.candidates_size() && c < kScan && idx[i].size() < kTop; ++c) {
      const converter::Candidate& cand = s.candidate(static_cast<int>(c));
      if (!seen.insert(cand.value).second) continue;
      idx[i].push_back(static_cast<int>(c));
      inputs[i].values.push_back(cand.value);
      inputs[i].costs.push_back(cand.cost);
    }
    if (idx[i].empty()) return false;
  }
  // 前の文脈：アプリから受け取ったカーソルの前の文字と突き合わせる（context_store.h）
  std::vector<std::string> ctx_keys;
  std::string ctx_text;
  if (request.config().imi_use_context()) {  // 設定で切れる（IMi のページ）
    ContextStore::Get().Snapshot(request.context().has_preceding_text(),
                                 request.context().preceding_text(), &ctx_keys, &ctx_text);
  }
  {
    std::string reading;
    for (const SegmentInput& in : inputs) reading += in.reading;
    DebugLog("convert", absl::StrCat("has_preceding=", request.context().has_preceding_text() ? "1" : "0",
                                     "\tpreceding=[", request.context().preceding_text(),
                                     "]\tkeys=[", absl::StrJoin(ctx_keys, " "),
                                     "]\treading=", reading));
  }
  const std::vector<int> choice = reranker_->Rerank(inputs, ctx_keys, lam_, lam2_);
  // order[i]：並べ替え後の順（inputs[i] の中の位置）。選んだ候補を先頭に、残りは元の順
  std::vector<std::vector<int>> order(n);
  for (size_t i = 0; i < n; ++i) {
    order[i].push_back(choice[i]);
    for (int p = 0; p < static_cast<int>(inputs[i].values.size()); ++p) {
      if (p != choice[i]) order[i].push_back(p);
    }
  }
  // 「いって・いった」などの「言」と「行」を、直前の名詞＋助詞で選び直す（imi-dev の ime_eval/verb_class.py と同じ）。
  // 「先生に・友達に」なら言、「学校に・東京に」なら行。表 verb_class.tsv（名詞・助詞・話す系・移動系の数）
  if (!verb_class_.empty()) {
    for (size_t i = 0; i < n; ++i) {
      const std::string& top = inputs[i].values[order[i][0]];
      const bool top_go = absl::StartsWith(top, "行"), top_say = absl::StartsWith(top, "言");
      if (!(top_go || top_say) || !absl::StartsWith(inputs[i].reading, "い")) continue;
      const absl::string_view rest = absl::string_view(top).substr(3);  // 「行」「言」は UTF-8 で3バイト
      const char* other = top_go ? "言" : "行";
      int q = -1;
      for (size_t p = 1; p < order[i].size(); ++p) {
        const std::string& v = inputs[i].values[order[i][p]];
        if (absl::StartsWith(v, other) && absl::string_view(v).substr(3) == rest) {
          q = static_cast<int>(p);
          break;
        }
      }
      if (q < 0) continue;
      const std::string left = i > 0 ? inputs[i - 1].values[order[i - 1][0]] : ctx_text;
      double r;
      if (!VerbRatio(left, &r)) continue;
      if ((top_go && r > verb_theta_) || (top_say && r < -verb_theta_)) {
        const int moved = order[i][q];
        order[i].erase(order[i].begin() + q);
        order[i].insert(order[i].begin(), moved);
      }
    }
  }
  const auto t1 = std::chrono::steady_clock::now();
  // B は変換（Space による変換と同時変換の表示）だけで使う。打鍵ごとの予測候補の中の変換では使わない
  if (lm_ && !SkipLm() && request.config().imi_use_lm()) {
    std::vector<std::string> cur(n);
    for (size_t i = 0; i < n; ++i) cur[i] = inputs[i].values[order[i][0]];
    std::string base;
    for (const std::string& c : cur) base += c;
    std::vector<AltSet> alts;
    for (size_t i = 0; i < n; ++i) {
      std::vector<int32_t> rc;
      for (int p : order[i]) rc.push_back(inputs[i].costs[p]);
      const std::vector<double> mc = MonotoneCosts(rc);
      std::vector<int> shown;
      for (size_t p = 0; p < std::min<size_t>(n_cand_, rc.size()); ++p) {
        if (mc[p] - mc[0] <= margin_) shown.push_back(static_cast<int>(p));
      }
      // 迷う文節：漢字を含む鍵が2種類以上。鍵が作れない候補でも、助詞で始まり、第1候補が漢字か
      // カタカナで始まるときは1種類として数える。名詞を確定した後に助詞から打つと「空メールが」と
      // 「からメールが」、「担ってました」と「になってました」で迷うため（記号や仮名書きの違いは数えない）
      if (shown.empty()) continue;  // 設定の n_cand・margin がおかしいとき
      const std::string& top = inputs[i].values[order[i][shown[0]]];
      absl::flat_hash_set<std::string> heads;
      for (int p : shown) {
        const std::string& v = inputs[i].values[order[i][p]];
        const std::string k = MakeKey(v, inputs[i].reading);
        if (k.find('|') != std::string::npos) {
          heads.insert(k);
        } else if (k.empty() && StartsWithParticle(v) && StartsWithKanjiOrKatakana(top)) {
          heads.insert("=" + v);
        }
      }
      if (heads.size() < 2) continue;
      AltSet a{static_cast<int>(i), {}};
      for (size_t x = 0; x < shown.size() && x < static_cast<size_t>(MaxAlt()); ++x) {
        const int p = shown[x];
        std::string text;
        for (size_t j = 0; j < n; ++j) text += j == i ? inputs[i].values[order[i][p]] : cur[j];
        a.texts.push_back({p, std::move(text)});
      }
      alts.push_back(std::move(a));
    }
    std::vector<AltScores> scores;
    if (!alts.empty() &&
        GetLmScores(LastChars(ctx_text, context_chars_), base, alts, &scores)) {
      for (const AltScores& as : scores) {
        float s0 = 0, best_v = 0;
        int best = -1;
        for (const auto& [p, v] : as.scores) {
          if (p == 0) s0 = v;
          if (best < 0 || v > best_v) {
            best = p;
            best_v = v;
          }
        }
        if (best > 0 && best_v - s0 > delta_) {
          std::vector<int>& o = order[as.segment];
          const int moved = o[best];
          o.erase(o.begin() + best);
          o.insert(o.begin(), moved);
        }
      }
    }
  }
  bool changed = false;
  for (size_t i = 0; i < n; ++i) {
    const int top = idx[i][order[i][0]];
    if (top != 0) {
      segments->mutable_conversion_segment(i)->move_candidate(top, 0);
      changed = true;
    }
  }
  // 確定した語の続きとして打った、助詞だけの最初の文節（「記入期間」＋「に｜相当する」の「に」）は、
  // ひらがなの助詞を第1候補にする。Mozc は文の先頭に助詞が来ることを想定しておらず、
  // 候補に入らないことがある（二・似・荷…だけになる）。前の文脈が句読点などで終わる・ないときはしない
  {
    Segment* seg = segments->mutable_conversion_segment(0);
    if (seg->candidates_size() > 0 && IsParticle(seg->key()) && EndsWithWordChar(ctx_text) &&
        seg->candidate(0).value != seg->key()) {
      int found = -1;
      for (size_t c = 0; c < seg->candidates_size(); ++c) {
        if (seg->candidate(static_cast<int>(c)).value == seg->key()) {
          found = static_cast<int>(c);
          break;
        }
      }
      if (found > 0) {
        seg->move_candidate(found, 0);
      } else {
        auto cand = std::make_unique<converter::Candidate>(seg->candidate(0));
        cand->value = std::string(seg->key());
        cand->content_value = cand->value;
        cand->content_key = cand->value;
        cand->description.clear();
        // 品詞は写した元の候補（「二」など）のままなので、学習には使わない
        cand->attributes |= converter::Attribute::NO_LEARNING;
        seg->insert_candidate(0, std::move(cand));
      }
      changed = true;
    }
  }
  if (timing_) {
    const auto t2 = std::chrono::steady_clock::now();
    std::cerr << "context_rerank_ms\t"
              << std::chrono::duration<double, std::milli>(t1 - t0).count() << "\t"
              << std::chrono::duration<double, std::milli>(t2 - t1).count() << std::endl;
  }
  return changed;
}

void ContextRerankRewriter::Finish(const ConversionRequest& request, const Segments& segments) {
  if (!reranker_) return;
  std::vector<std::pair<std::string, std::string>> committed;
  for (const Segment& s : segments.conversion_segments()) {
    if (s.candidates_size() == 0) continue;
    committed.push_back({s.candidate(0).value, std::string(s.key())});
  }
  std::string text;
  for (const auto& [v, r] : committed) text += v;
  DebugLog("commit", text);
  ContextStore::Get().Commit(committed);
}

}  // namespace mozc::context_rerank
