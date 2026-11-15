// IMi：利用者が直した入れ替えの記録（choice_store.h）を確かめる

#include "context_rerank/choice_store.h"

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "testing/gunit.h"

namespace mozc::context_rerank {
namespace {

class ChoiceStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::StatusOr<TempDirectory> dir = TempDirectory::Default().CreateTempDirectory();
    ASSERT_TRUE(dir.ok());
    dir_ = std::make_unique<TempDirectory>(*std::move(dir));
    filename_ = FileUtil::JoinPath(dir_->path(), "imi_choice.db");
  }
  std::unique_ptr<TempDirectory> dir_;
  std::string filename_;
};

TEST_F(ChoiceStoreTest, NothingLearned) {
  ChoiceStore store(filename_);
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "後悔"));
  EXPECT_FALSE(store.Prefers("こうかい", "後悔", "公開"));
}

// 直した向きだけを止める
TEST_F(ChoiceStoreTest, LearnedDirection) {
  ChoiceStore store(filename_);
  store.Learn("こうかい", "公開", "後悔");
  EXPECT_TRUE(store.Prefers("こうかい", "公開", "後悔"));
  EXPECT_FALSE(store.Prefers("こうかい", "後悔", "公開"));
  // 同じ向きに何度直しても同じ
  store.Learn("こうかい", "公開", "後悔");
  EXPECT_TRUE(store.Prefers("こうかい", "公開", "後悔"));
  // 別の読み・別の語の組には効かない
  EXPECT_FALSE(store.Prefers("こうかいを", "公開", "後悔"));
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "航海"));
}

// 文字の順が逆の組でも同じ動き
TEST_F(ChoiceStoreTest, LearnedDirectionReversedOrder) {
  ChoiceStore store(filename_);
  store.Learn("こうかい", "後悔", "公開");
  EXPECT_TRUE(store.Prefers("こうかい", "後悔", "公開"));
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "後悔"));
}

// 逆向きにも直したら、どちらの向きも止めない。その後に何度直しても変わらない
TEST_F(ChoiceStoreTest, BothDirections) {
  ChoiceStore store(filename_);
  store.Learn("こうかい", "公開", "後悔");
  store.Learn("こうかい", "後悔", "公開");
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "後悔"));
  EXPECT_FALSE(store.Prefers("こうかい", "後悔", "公開"));
  store.Learn("こうかい", "公開", "後悔");
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "後悔"));
  EXPECT_FALSE(store.Prefers("こうかい", "後悔", "公開"));
}

TEST_F(ChoiceStoreTest, IgnoresEmptyAndSame) {
  ChoiceStore store(filename_);
  store.Learn("", "公開", "後悔");
  store.Learn("こうかい", "", "後悔");
  store.Learn("こうかい", "公開", "");
  store.Learn("こうかい", "公開", "公開");
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "後悔"));
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "公開"));
}

TEST_F(ChoiceStoreTest, Clear) {
  ChoiceStore store(filename_);
  store.Learn("こうかい", "公開", "後悔");
  store.Clear();
  EXPECT_FALSE(store.Prefers("こうかい", "公開", "後悔"));
}

// ファイルに残り、開き直しても覚えている
TEST_F(ChoiceStoreTest, Persists) {
  {
    ChoiceStore store(filename_);
    store.Learn("こうかい", "公開", "後悔");
  }
  ChoiceStore store(filename_);
  EXPECT_TRUE(store.Prefers("こうかい", "公開", "後悔"));
}

}  // namespace
}  // namespace mozc::context_rerank
