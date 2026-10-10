// IMi：同時変換（打鍵ごとに変換結果を表示し、Enter でそのまま確定する）の確定を確かめる。
// 変換エンジンは作り物（MockConverter）で、表示用の変換と確定のときの変換の区切りを決めて与える。
// 同時変換は rerank_config.txt の live_conversion で有効になるので、資料置き場を一時フォルダーに
// 差し替え、「live_conversion 1」だけを書いた設定を置く（最初の読み込みの前に行う）。
// 設定は1つのプログラムで最初に読んだ値を使い続けるので、このファイルは単独のテストにする。

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/file/temp_dir.h"
#include "base/file_util.h"
#include "context_rerank/live_conversion.h"
#include "composer/composer.h"
#include "composer/table.h"
#include "converter/candidate.h"
#include "converter/converter_mock.h"
#include "converter/segments.h"
#include "engine/engine_converter.h"
#include "protocol/commands.pb.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gmock.h"
#include "testing/gunit.h"

namespace mozc {
namespace engine {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgPointee;

// 読みと表示の組から、文節ごとに候補1つの Segments を作る
Segments MakeSegments(
    const std::vector<std::pair<absl::string_view, absl::string_view>>& segs) {
  Segments segments;
  for (const auto& [key, value] : segs) {
    Segment* seg = segments.add_segment();
    seg->set_key(key);
    converter::Candidate* cand = seg->add_candidate();
    cand->key = std::string(key);
    cand->content_key = cand->key;
    cand->value = std::string(value);
    cand->content_value = cand->value;
  }
  return segments;
}

class EngineConverterLiveTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    absl::StatusOr<TempDirectory> dir = TempDirectory::Default().CreateTempDirectory();
    ASSERT_TRUE(dir.ok()) << dir.status();
    asset_dir_ = new TempDirectory(*std::move(dir));
    const char* old_dir = std::getenv("MOZC_CONTEXT_RERANK_DIR");
    old_dir_ = old_dir != nullptr ? new std::string(old_dir) : nullptr;
    // 据え置きを解くまでの時間は長めにし、テストの実行の速さで結果が変わらないようにする
    ASSERT_TRUE(FileUtil::SetContents(
                    FileUtil::JoinPath(asset_dir_->path(), "rerank_config.txt"),
                    "live_conversion 1\nlive_hold_release_ms 2000\n")
                    .ok());
#ifdef _WIN32
    _putenv_s("MOZC_CONTEXT_RERANK_DIR", asset_dir_->path().c_str());
#else
    setenv("MOZC_CONTEXT_RERANK_DIR", asset_dir_->path().c_str(), 1);
#endif  // _WIN32
  }

  static void TearDownTestSuite() {
    const std::string old_dir = old_dir_ != nullptr ? *old_dir_ : "";
#ifdef _WIN32
    _putenv_s("MOZC_CONTEXT_RERANK_DIR", old_dir.c_str());  // 空なら消す
#else
    if (old_dir_ != nullptr) {
      setenv("MOZC_CONTEXT_RERANK_DIR", old_dir.c_str(), 1);
    } else {
      unsetenv("MOZC_CONTEXT_RERANK_DIR");
    }
#endif  // _WIN32
    delete old_dir_;
    old_dir_ = nullptr;
    delete asset_dir_;
    asset_dir_ = nullptr;
  }

  void SetUp() override {
    config_ = std::make_shared<config::Config>();
    request_ = std::make_shared<commands::Request>();
    table_ = std::make_shared<composer::Table>();
    table_->InitializeWithRequestAndConfig(*request_, *config_);
    composer_ = std::make_unique<composer::Composer>(table_, *request_, *config_);
    mock_converter_ = std::make_shared<MockConverter>();
    converter_ = std::make_unique<EngineConverter>(mock_converter_, request_, config_);
  }

  std::string Preedit() const {
    commands::Output output;
    converter_->FillOutput(*composer_, &output);
    std::string preedit;
    for (const auto& segment : output.preedit().segment()) preedit += segment.value();
    return preedit;
  }

  static TempDirectory* asset_dir_;
  static std::string* old_dir_;
  std::shared_ptr<config::Config> config_;
  std::shared_ptr<commands::Request> request_;
  std::shared_ptr<composer::Table> table_;
  std::unique_ptr<composer::Composer> composer_;
  std::shared_ptr<MockConverter> mock_converter_;
  std::unique_ptr<EngineConverter> converter_;
  commands::Context context_;
};

TempDirectory* EngineConverterLiveTest::asset_dir_ = nullptr;
std::string* EngineConverterLiveTest::old_dir_ = nullptr;

// 確定のときの変換の区切りが表示と違うとき、表示の区切りに合わせて確定する。
// 合わせたことは利用者が区切りを選んだ印（resized）として残さない（残すと区切りを学習してしまう）
TEST_F(EngineConverterLiveTest, CommitMatchesShownSplitWithoutResizedMark) {
  composer_->InsertCharacterPreedit("あいう");
  EXPECT_CALL(*mock_converter_, StartConversion(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "亜"}, {"いう", "言う"}})),
                      Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あい", "愛"}, {"う", "鵜"}})),
                      Return(true)));
  converter_->UpdateLivePreedit(*composer_, context_);
  EXPECT_EQ(Preedit(), "亜言う");

  std::vector<uint8_t> sizes;
  EXPECT_CALL(*mock_converter_, ResizeSegments(_, _, 0, _))
      .WillOnce(Invoke([&sizes](Segments* segments, const ConversionRequest&, size_t,
                                absl::Span<const uint8_t> new_sizes) {
        sizes.assign(new_sizes.begin(), new_sizes.end());
        *segments = MakeSegments({{"あ", "亜"}, {"いう", "言う"}});
        segments->set_resized(true);
        return true;
      }));
  bool resized_at_finish = true;
  EXPECT_CALL(*mock_converter_, FinishConversion(_, _))
      .WillOnce(Invoke([&resized_at_finish](const ConversionRequest&, Segments* segments) {
        resized_at_finish = segments->resized();
      }));
  ASSERT_TRUE(converter_->CommitLivePreedit(*composer_, context_));

  EXPECT_THAT(sizes, ElementsAre(1, 2));
  EXPECT_FALSE(resized_at_finish);
  commands::Output output;
  converter_->FillOutput(*composer_, &output);
  EXPECT_EQ(output.result().value(), "亜言う");
}

// 1文字で表示済みの前半が別の区切りで書き換わるときは、前半を前の表示のまま残し、後ろを仮名で
// 見せる（据え置き）。据え置き中に Enter を押したら、据え置きを解いた表示（手を止めたら出る文）で
// 確定し、仮名のままにはしない
TEST_F(EngineConverterLiveTest, CommitWhileHoldingUsesReleasedDisplay) {
  composer_->InsertCharacterPreedit("あいう");
  EXPECT_CALL(*mock_converter_, StartConversion(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "亜"}, {"いう", "言う"}})),
                      Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あい", "愛"}, {"うえ", "上"}})),
                      Return(true)))
      // 確定のとき：据え置きを解いた表示を作る変換
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あい", "愛"}, {"うえ", "上"}})),
                      Return(true)))
      // 確定のとき：確定する文を作る変換（表示と違う候補にして、表示に合わせることを確かめる）
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あい", "相"}, {"うえ", "植"}})),
                      Return(true)));
  converter_->UpdateLivePreedit(*composer_, context_);
  EXPECT_EQ(Preedit(), "亜言う");

  composer_->InsertCharacterPreedit("え");
  converter_->UpdateLivePreedit(*composer_, context_);
  EXPECT_EQ(Preedit(), "亜いうえ");  // 据え置き中

  EXPECT_CALL(*mock_converter_, ResizeSegments(_, _, _, _)).Times(0);
  ASSERT_TRUE(converter_->CommitLivePreedit(*composer_, context_));
  commands::Output output;
  converter_->FillOutput(*composer_, &output);
  EXPECT_EQ(output.result().value(), "愛上");
}

// 末尾にまだ仮名になっていないローマ字があるとき（「あいうｋ」）、表示はその前までを変換して
// ローマ字を後ろに付ける。確定のときの読みは表示と違う（「k」も含む）ので区切りは変えず、
// 読みが同じ前の文節だけを表示に合わせる。変換し直した候補に表示した字がなければ、表示の候補を足す
TEST_F(EngineConverterLiveTest, CommitWithTrailingRomajiMatchesLeadingSegments) {
  composer_->InsertCharacterPreedit("あいうk");
  EXPECT_CALL(*mock_converter_, StartConversion(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "亜"}, {"いう", "言う"}})),
                      Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "阿"}, {"いうｋ", "いうｋ"}})),
                      Return(true)));
  converter_->UpdateLivePreedit(*composer_, context_);
  EXPECT_EQ(Preedit(), "亜言うｋ");

  EXPECT_CALL(*mock_converter_, ResizeSegments(_, _, _, _)).Times(0);
  ASSERT_TRUE(converter_->CommitLivePreedit(*composer_, context_));
  commands::Output output;
  converter_->FillOutput(*composer_, &output);
  EXPECT_EQ(output.result().value(), "亜いうｋ");
}

// B（小型言語モデル）の結果がまだのあいだは、区切りが同じ前半の文節に前回の表示（B の判断済み）を
// 引き継ぐ。確定のときの変換も B の結果がまだ（T0+K5 の候補）でも、表示した字で確定する
TEST_F(EngineConverterLiveTest, CommitWhileLmPendingUsesShownCandidates) {
  composer_->InsertCharacterPreedit("あいう");
  EXPECT_CALL(*mock_converter_, StartConversion(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "亜"}, {"いう", "言う"}})),
                      Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "阿"}, {"いうえ", "言うえ"}})),
                      Invoke([] { context_rerank::SetLivePending(); }), Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(MakeSegments({{"あ", "阿"}, {"いうえ", "言うえ"}})),
                      Return(true)));
  converter_->UpdateLivePreedit(*composer_, context_);
  EXPECT_EQ(Preedit(), "亜言う");

  composer_->InsertCharacterPreedit("え");
  converter_->UpdateLivePreedit(*composer_, context_);
  EXPECT_EQ(Preedit(), "亜言うえ");  // 前の文節は前回の表示を引き継ぐ

  ASSERT_TRUE(converter_->CommitLivePreedit(*composer_, context_));
  commands::Output output;
  converter_->FillOutput(*composer_, &output);
  EXPECT_EQ(output.result().value(), "亜言うえ");
}

}  // namespace
}  // namespace engine
}  // namespace mozc
