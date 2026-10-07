// B の C++ 版が Python 版と同じ点数を出すか照合し、速さを測る。
//   lm_scorer_main --assets=<資料> --cases=<lm_cases.tsv> --threads=2
// lm_cases.tsv（imi-dev（開発用リポジトリ）の phase0/corpus/export_lm_cases.py）：
//   C\t前の文脈\t現在の文 / A\t文節\t候補\t文\t点数 / E

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/init_mozc.h"
#include "context_rerank/lm_scorer.h"

ABSL_FLAG(std::string, assets, "", "資料のディレクトリ");
ABSL_FLAG(std::string, cases, "", "照合用データ");
ABSL_FLAG(int32_t, threads, 2, "ONNX Runtime のスレッド数");
ABSL_FLAG(int32_t, window, 4, "入れ替え箇所の後ろのトークン数");
ABSL_FLAG(std::string, precision, "int8", "int8 または fp32");

using mozc::context_rerank::AltScores;
using mozc::context_rerank::AltSet;

namespace {

struct Case {
  std::string prefix, base;
  std::vector<AltSet> alts;
  std::map<std::pair<int, int>, float> want;
};

float Diff(const std::vector<std::pair<int, float>>& s, int j) {
  float s0 = 0, sj = 0;
  for (const auto& [k, v] : s) {
    if (k == 0) s0 = v;
    if (k == j) sj = v;
  }
  return sj - s0;
}

}  // namespace

int main(int argc, char** argv) {
  mozc::InitMozc(argv[0], &argc, &argv);
  mozc::context_rerank::LmScorer lm;
  if (!lm.Load(absl::GetFlag(FLAGS_assets), absl::GetFlag(FLAGS_threads), absl::GetFlag(FLAGS_precision))) {
    std::cerr << "資料を読めません" << std::endl;
    return 1;
  }
  std::vector<Case> cases;
  std::ifstream f(absl::GetFlag(FLAGS_cases));
  std::string line;
  Case cur;
  while (std::getline(f, line)) {
    std::vector<std::string> x = absl::StrSplit(line, '\t');
    if (x[0] == "C") {
      cur = Case{x[1], x[2], {}, {}};
    } else if (x[0] == "A") {
      int k, j;
      float v;
      if (!absl::SimpleAtoi(x[1], &k) || !absl::SimpleAtoi(x[2], &j) || !absl::SimpleAtof(x[4], &v)) continue;
      if (cur.alts.empty() || cur.alts.back().segment != k) cur.alts.push_back({k, {}});
      cur.alts.back().texts.push_back({j, x[3]});
      cur.want[{k, j}] = v;
    } else if (x[0] == "E") {
      cases.push_back(cur);
    }
  }
  // 温める
  for (size_t i = 0; i < std::min<size_t>(5, cases.size()); ++i) {
    lm.Score(cases[i].prefix, cases[i].base, cases[i].alts, absl::GetFlag(FLAGS_window));
  }
  std::vector<double> ms;
  double worst = 0;
  int n_cmp = 0, agree = 0, n_seg = 0, n_bad = 0, n_bad_total = 0;
  for (const Case& c : cases) {
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<AltScores> got = lm.Score(c.prefix, c.base, c.alts, absl::GetFlag(FLAGS_window));
    ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    for (const AltScores& a : got) {
      // Python 版の点数を同じ形にする
      std::vector<std::pair<int, float>> want;
      for (const auto& [j, v] : a.scores) want.push_back({j, c.want.at({a.segment, j})});
      int best_got = 0, best_want = 0;
      float bg = 0, bw = 0;
      for (const auto& [j, v] : a.scores) {
        if (j == 0) continue;
        const float dg = Diff(a.scores, j), dw = Diff(want, j);
        if (std::fabs(dg - dw) > 0.05 && ++n_bad <= 5) {
          std::cout << "ずれ: 文節" << a.segment << " 候補" << j << " C++ " << dg << " / Python " << dw
                    << "\n  前: " << c.prefix << "\n  文: " << c.base << std::endl;
        }
        if (std::fabs(dg - dw) > 0.05) ++n_bad_total;
        worst = std::max(worst, static_cast<double>(std::fabs(dg - dw)));
        ++n_cmp;
        if (dg > 2 && dg > bg) { bg = dg; best_got = j; }  // δ=2 で入れ替える候補
        if (dw > 2 && dw > bw) { bw = dw; best_want = j; }
      }
      ++n_seg;
      agree += best_got == best_want;
    }
  }
  std::sort(ms.begin(), ms.end());
  auto q = [&](double p) { return ms[std::min(ms.size() - 1, static_cast<size_t>(ms.size() * p))]; };
  std::cout << cases.size() << "文、" << n_cmp << "候補で Python 版との点数差の最大ずれ " << worst
            << "（0.05を超える候補 " << n_bad_total << "）、入れ替えの判断の一致 " << agree << "/" << n_seg << "\n"
            << "1文あたり 中央値 " << q(0.5) << "ms・90%点 " << q(0.9) << "ms・99%点 " << q(0.99)
            << "ms（スレッド " << absl::GetFlag(FLAGS_threads) << "）" << std::endl;
  return 0;
}
