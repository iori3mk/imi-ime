// 鍵の C++ 版が Python 版（ime_eval/mkey.py）と同じか照合する。
//   mkey_main --golden=<mkey_golden.tsv>   （表記\t読み\t鍵。鍵なしは空）

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/str_split.h"
#include "base/init_mozc.h"
#include "context_rerank/mkey.h"

ABSL_FLAG(std::string, golden, "", "照合用データ");

int main(int argc, char** argv) {
  mozc::InitMozc(argv[0], &argc, &argv);
  std::ifstream f(absl::GetFlag(FLAGS_golden));
  std::string line;
  int total = 0, bad = 0;
  while (std::getline(f, line)) {
    std::vector<std::string> x = absl::StrSplit(line, '\t');
    if (x.size() != 3) continue;
    ++total;
    const std::string got = mozc::context_rerank::MakeKey(x[0], x[1]);
    if (got != x[2] && ++bad <= 5) {
      std::cout << "不一致: " << x[0] << " / " << x[1] << " 期待「" << x[2] << "」 結果「" << got << "」" << std::endl;
    }
  }
  std::cout << total << "組中 不一致 " << bad << std::endl;
  return bad == 0 ? 0 : 2;
}
