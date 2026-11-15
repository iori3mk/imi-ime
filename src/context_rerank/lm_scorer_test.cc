// IMi：小型言語モデル（B）の読み込みを確かめる。モデルのファイルがない・壊れているときは読み込みを
// 失敗にし、プログラムを止めないこと（読めなければ B を使わず、IME 自体は動く）

#include "context_rerank/lm_scorer.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "testing/gunit.h"

#ifdef _WIN32
#include <windows.h>
#endif  // _WIN32

namespace mozc::context_rerank {
namespace {

class LmScorerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<TempDirectory> dir = TempDirectory::Default().CreateTempDirectory();
    ASSERT_TRUE(dir.ok());
    dir_ = std::make_unique<TempDirectory>(*std::move(dir));
    // トークン化と文頭の状態の資料は正しい形で置く（モデルの読み込みまで進ませる）
    Put("pieces.tsv", "0\t0\t<unk>\n1\t0\t<s>\n2\t0\t</s>\n3\t-1.5\t\xE2\x96\x81\n4\t-2.5\tあ\n");
    Put("norm.tsv", "");
    Put("bos_kv.f32", std::string(4 * 16, '\0'));
    Put("bos_lp.f32", std::string(4 * 5, '\0'));
#ifdef _WIN32
    // onnxruntime.dll がなければ、モデルを読む前に失敗して何も確かめられない
    ASSERT_NE(::LoadLibraryExW(L"onnxruntime.dll", nullptr,
                               LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32),
              nullptr);
#endif  // _WIN32
  }
  void Put(const std::string& name, const std::string& contents) {
    ASSERT_TRUE(FileUtil::SetContents(FileUtil::JoinPath(dir_->path(), name), contents).ok());
  }
  std::unique_ptr<TempDirectory> dir_;
};

TEST_F(LmScorerTest, MissingModelFails) {
  LmScorer lm;
  EXPECT_FALSE(lm.Load(dir_->path(), 1));
}

TEST_F(LmScorerTest, BrokenModelFails) {
  Put("basep_int8.onnx", "this is not an onnx model");
  Put("alt_int8.onnx", "this is not an onnx model");
  LmScorer lm;
  EXPECT_FALSE(lm.Load(dir_->path(), 1));
}

TEST_F(LmScorerTest, EmptyModelFails) {
  Put("basep_int8.onnx", "");
  Put("alt_int8.onnx", "");
  LmScorer lm;
  EXPECT_FALSE(lm.Load(dir_->path(), 1));
}

// 読み込みに失敗した後に点数を頼まれても、止まらずに空を返す
TEST_F(LmScorerTest, ScoreWithoutModelReturnsEmpty) {
  LmScorer lm;
  ASSERT_FALSE(lm.Load(dir_->path(), 1));
  const std::vector<AltSet> alts = {{0, {{0, "あ"}, {1, "ああ"}}}};
  EXPECT_TRUE(lm.Score("", "あ", alts, 4).empty());
}

}  // namespace
}  // namespace mozc::context_rerank
