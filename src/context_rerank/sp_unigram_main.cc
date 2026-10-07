// 自前のトークナイザーが Python 版（transformers）と同じトークン列を出すか照合する。
//   sp_unigram_main --assets=<lm_assets> --golden=<golden.tsv>
// golden.tsv：文\tトークン番号（空白区切り）

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "base/init_mozc.h"
#include "context_rerank/sp_unigram.h"

ABSL_FLAG(std::string, assets, "", "pieces.tsv・norm.tsv のあるディレクトリ");
ABSL_FLAG(std::string, golden, "", "照合用の文とトークン列");

int main(int argc, char** argv) {
  mozc::InitMozc(argv[0], &argc, &argv);
  const std::string dir = absl::GetFlag(FLAGS_assets);
  mozc::context_rerank::SpUnigram sp;
  if (!sp.Load(dir + "/pieces.tsv", dir + "/norm.tsv", 0, 2)) {
    std::cerr << "資料を読めません: " << dir << std::endl;
    return 1;
  }
  std::ifstream gf(absl::GetFlag(FLAGS_golden));
  std::string line;
  int total = 0, bad = 0;
  int64_t ns = 0;
  while (std::getline(gf, line)) {
    std::vector<absl::string_view> f = absl::StrSplit(line, '\t');
    if (f.size() != 2) continue;
    std::vector<int> want;
    for (absl::string_view x : absl::StrSplit(f[1], ' ', absl::SkipEmpty())) {
      int v;
      if (absl::SimpleAtoi(x, &v)) want.push_back(v);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const std::vector<int> got = sp.Encode(f[0]);
    ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - t0)
              .count();
    ++total;
    if (got != want) {
      if (++bad <= 5) {
        std::cout << "不一致: " << f[0] << "\n  期待 " << absl::StrJoin(want, " ")
                  << "\n  結果 " << absl::StrJoin(got, " ") << std::endl;
      }
    }
  }
  std::cout << total << "文中 不一致 " << bad << "、1文あたり平均 "
            << (total ? ns / total / 1000.0 : 0) << "μs" << std::endl;
  return bad == 0 ? 0 : 2;
}
