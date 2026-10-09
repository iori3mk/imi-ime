// IMi：表（tables.bin）の読み込みを確かめる。途中で切れた・壊れたファイルは読み込みを失敗にし、
// 範囲の外を読まないこと（読めなければ表なしの Mozc と同じ動きに戻る）

#include "context_rerank/context_reranker.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "testing/gunit.h"

namespace mozc::context_rerank {
namespace {

void PutU32(std::string* s, uint32_t v) { s->append(reinterpret_cast<const char*>(&v), 4); }
void PutF64(std::string* s, double v) { s->append(reinterpret_cast<const char*>(&v), 8); }

// 鍵2つ（「あ」「い」）、共起の組1つ（あ → い が 5回）、隣の表は空の、正しい tables.bin
std::string ValidTables() {
  std::string s = "CTXRR002";
  PutU32(&s, 2);  // 鍵の数
  PutU32(&s, 1);  // 共起の組の数
  PutU32(&s, 0);  // 左隣の組の数
  PutU32(&s, 0);  // 右隣の組の数
  PutU32(&s, 3);  // 鍵の終わり（UTF-8 のバイト数）
  PutU32(&s, 6);
  s += "あい";
  for (double v : {1.0, -1.0}) PutF64(&s, v);  // 対象語の共起数の合計（負は行なし）
  for (double v : {1.0, 1.0}) PutF64(&s, v);   // 背景の数
  s.append(2, '\0');                            // 止め語
  for (int t = 0; t < 3; ++t) {
    for (double v : {0.0, 0.0}) PutF64(&s, v);  // 隣の表の数
  }
  for (uint32_t o : {0u, 1u, 1u}) PutU32(&s, o);  // 共起：行の始まり
  PutU32(&s, 1);                                  // （語, 数）
  PutU32(&s, 5);
  for (int t = 0; t < 2; ++t) {
    for (uint32_t o : {0u, 0u, 0u}) PutU32(&s, o);  // 左隣・右隣：行の始まり（組なし）
  }
  return s;
}

class ContextRerankerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<TempDirectory> dir = TempDirectory::Default().CreateTempDirectory();
    ASSERT_TRUE(dir.ok());
    dir_ = std::make_unique<TempDirectory>(*std::move(dir));
  }
  bool LoadTables(const std::string& contents) {
    EXPECT_TRUE(FileUtil::SetContents(FileUtil::JoinPath(dir_->path(), "tables.bin"), contents).ok());
    ContextReranker reranker;
    return reranker.Load(dir_->path());
  }
  std::unique_ptr<TempDirectory> dir_;
};

TEST_F(ContextRerankerTest, LoadsValidTables) {
  ContextReranker reranker;
  ASSERT_TRUE(FileUtil::SetContents(FileUtil::JoinPath(dir_->path(), "tables.bin"), ValidTables()).ok());
  EXPECT_TRUE(reranker.Load(dir_->path()));
  EXPECT_EQ(reranker.num_pairs(), 1);
}

TEST_F(ContextRerankerTest, RejectsTruncatedTables) {
  const std::string valid = ValidTables();
  for (size_t len = 0; len < valid.size(); ++len) {
    EXPECT_FALSE(LoadTables(valid.substr(0, len))) << "長さ " << len;
  }
}

TEST_F(ContextRerankerTest, RejectsExtraBytes) { EXPECT_FALSE(LoadTables(ValidTables() + "x")); }

TEST_F(ContextRerankerTest, RejectsCorruptTables) {
  const std::string valid = ValidTables();
  auto with_u32 = [&](size_t offset, uint32_t v) {
    std::string s = valid;
    std::memcpy(s.data() + offset, &v, 4);
    return s;
  };
  EXPECT_FALSE(LoadTables(with_u32(8, 0x7fffffff)));  // 鍵の数がファイルより大きい
  EXPECT_FALSE(LoadTables(with_u32(12, 1000)));       // 共起の組の数がファイルより大きい
  EXPECT_FALSE(LoadTables(with_u32(24, 7)));          // 鍵の終わりが減る
  EXPECT_FALSE(LoadTables(with_u32(28, 100)));        // 鍵の終わりがファイルの外
  // 共起の行の始まり（止め語の後ろ）
  const size_t off = 8 + 16 + 8 + 6 + 8 * 2 * 2 + 2 + 8 * 2 * 3;
  EXPECT_FALSE(LoadTables(with_u32(off, 1)));      // 最初の行が 0 から始まらない
  EXPECT_FALSE(LoadTables(with_u32(off + 4, 2)));  // 行の始まりが減る
  EXPECT_FALSE(LoadTables(with_u32(off + 8, 0)));  // 最後が組の数と合わない
  EXPECT_TRUE(LoadTables(valid));
}

}  // namespace
}  // namespace mozc::context_rerank
