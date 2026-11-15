// IMi：利用者が候補を選び直して確定した組は、文脈による並べ替えで元に戻さないことを確かめる。
// 小さな表（「後悔」は「反省」と一緒に出る）を作り、小型言語モデルは使わない設定で動かす。

#include "context_rerank/context_rerank_rewriter.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "base/file_util.h"
#include "base/system_util.h"
#include "config/config_handler.h"
#include "context_rerank/context_store.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "testing/gunit.h"
#include "testing/mozctest.h"

namespace mozc::context_rerank {
namespace {

void PutU32(std::string* s, uint32_t v) { s->append(reinterpret_cast<const char*>(&v), 4); }
void PutF64(std::string* s, double v) { s->append(reinterpret_cast<const char*>(&v), 8); }

// 鍵3つ（公開・後悔・反省）、共起の組1つ（後悔 → 反省 が 10回）、隣の表は空の tables.bin
std::string Tables() {
  const std::vector<std::string> keys = {"公開|こうかい", "後悔|こうかい", "反省|はんせい"};
  std::string s = "CTXRR002";
  PutU32(&s, 3);  // 鍵の数
  PutU32(&s, 1);  // 共起の組の数
  PutU32(&s, 0);  // 左隣の組の数
  PutU32(&s, 0);  // 右隣の組の数
  uint32_t end = 0;
  for (const std::string& k : keys) PutU32(&s, end += static_cast<uint32_t>(k.size()));
  for (const std::string& k : keys) s += k;
  for (double v : {10.0, 10.0, -1.0}) PutF64(&s, v);  // 対象語の共起数の合計（負は行なし）
  for (double v : {1.0, 1.0, 1.0}) PutF64(&s, v);     // 背景の数
  s.append(3, '\0');                                  // 止め語
  for (int t = 0; t < 3; ++t) {
    for (int i = 0; i < 3; ++i) PutF64(&s, 0.0);  // 隣の表の数
  }
  for (uint32_t o : {0u, 0u, 1u, 1u}) PutU32(&s, o);  // 共起：行の始まり
  PutU32(&s, 2);                                      // （反省, 10回）
  PutU32(&s, 10);
  for (int t = 0; t < 2; ++t) {
    for (int i = 0; i < 4; ++i) PutU32(&s, 0);  // 左隣・右隣：行の始まり（組なし）
  }
  return s;
}

void SetEnv(const std::string& name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name.c_str(), value.c_str());
#else
  setenv(name.c_str(), value.c_str(), 1);
#endif  // _WIN32
}

void AddCandidate(Segment* segment, const std::string& key, const std::string& value,
                  const std::string& content_key, const std::string& content_value) {
  converter::Candidate* c = segment->add_candidate();
  c->key = key;
  c->value = value;
  c->content_key = content_key;
  c->content_value = content_value;
  c->cost = 1000;
}

class ContextRerankRewriterTest : public testing::TestWithTempUserProfile {
 protected:
  void SetUp() override {
    // 表と設定は設定の場所（テストごとの一時フォルダ）に置く
    const std::string dir = SystemUtil::GetUserProfileDirectory();
    ASSERT_TRUE(FileUtil::SetContents(FileUtil::JoinPath(dir, "tables.bin"), Tables()).ok());
    ASSERT_TRUE(FileUtil::SetContents(FileUtil::JoinPath(dir, "rerank_config.txt"),
                                      "lam 0.5\nlam2 0\nuse_lm 0\n")
                    .ok());
    SetEnv("MOZC_CONTEXT_RERANK_DIR", dir);
    ContextStore::Get().Clear();
    config::ConfigHandler::GetDefaultConfig(&config_);
    rewriter_ = std::make_unique<ContextRerankRewriter>();
  }
  void TearDown() override {
    rewriter_.reset();
    ContextStore::Get().Clear();
  }

  ConversionRequest Request() const {
    return ConversionRequestBuilder().SetConfig(config_).Build();
  }

  // 「こうかい」（first が第1候補）と「はんせい」の2文節。suffix は1つ目の文節の付属語（「を」など）
  static Segments MakeSegments(const std::string& first, const std::string& suffix = "") {
    const std::string second = first == "公開" ? "後悔" : "公開";
    Segments segments;
    Segment* s = segments.add_segment();
    s->set_key("こうかい" + suffix);
    AddCandidate(s, "こうかい" + suffix, first + suffix, "こうかい", first);
    AddCandidate(s, "こうかい" + suffix, second + suffix, "こうかい", second);
    s = segments.add_segment();
    s->set_key("はんせい");
    AddCandidate(s, "はんせい", "反省", "はんせい", "反省");
    return segments;
  }

  // 並べ替えた後の、1つ目の文節の第1候補
  std::string Top(const std::string& first, const std::string& suffix = "") const {
    Segments segments = MakeSegments(first, suffix);
    rewriter_->Rewrite(Request(), &segments);
    return segments.conversion_segment(0).candidate(0).value;
  }

  // first が出ていたところを、利用者が2番目の候補に選び直して確定する
  void Correct(const std::string& first, const ConversionRequest& request) {
    Segments segments = MakeSegments(first);
    Segment* s = segments.mutable_conversion_segment(0);
    s->move_candidate(1, 0);
    s->mutable_candidate(0)->attributes |= converter::Attribute::RERANKED;
    rewriter_->Finish(request, segments);
  }

  config::Config config_;
  std::unique_ptr<ContextRerankRewriter> rewriter_;
};

// 何も直していなければ、文脈で選ぶ（「反省」の前は「後悔」）
TEST_F(ContextRerankRewriterTest, ContextChoosesWithoutCorrection) {
  EXPECT_EQ(Top("公開"), "後悔");
  EXPECT_EQ(Top("後悔"), "後悔");
}

// 「後悔」を「公開」に直して確定したら、「公開」が第1候補のときに「後悔」へ戻さない
TEST_F(ContextRerankRewriterTest, KeepsCorrectedChoice) {
  Correct("後悔", Request());
  EXPECT_EQ(Top("公開"), "公開");
  // 付属語の付いた文節でも同じ（内容語の組で覚えている）
  EXPECT_EQ(Top("公開", "を"), "公開を");
  // 「後悔」が第1候補のときは何もしない（第1候補を上げるのは Mozc の学習の役目）
  EXPECT_EQ(Top("後悔"), "後悔");
}

// 逆向きにも直したら、両方を使う語として、また文脈で選ぶ
TEST_F(ContextRerankRewriterTest, ContextChoosesAgainAfterBothDirections) {
  Correct("後悔", Request());
  ASSERT_EQ(Top("公開"), "公開");
  Correct("公開", Request());
  EXPECT_EQ(Top("公開"), "後悔");
  EXPECT_EQ(Top("後悔"), "後悔");
}

// 選び直さずに確定しただけ（第1候補のまま）では覚えない
TEST_F(ContextRerankRewriterTest, PlainCommitIsNotLearned) {
  Segments segments = MakeSegments("公開");
  rewriter_->Finish(Request(), segments);
  ContextStore::Get().Clear();
  EXPECT_EQ(Top("公開"), "後悔");
}

// 学習履歴の消去で忘れる
TEST_F(ContextRerankRewriterTest, ClearForgets) {
  Correct("後悔", Request());
  ASSERT_EQ(Top("公開"), "公開");
  rewriter_->Clear();
  EXPECT_EQ(Top("公開"), "後悔");
}

// シークレットモード・学習しない設定では覚えない
TEST_F(ContextRerankRewriterTest, NotLearnedWhenHistoryIsOff) {
  config_.set_incognito_mode(true);
  Correct("後悔", Request());
  config_.set_incognito_mode(false);
  EXPECT_EQ(Top("公開"), "後悔");

  config_.set_history_learning_level(config::Config::READ_ONLY);
  Correct("後悔", Request());
  config_.set_history_learning_level(config::Config::DEFAULT_HISTORY);
  EXPECT_EQ(Top("公開"), "後悔");
}

// 覚えた後でも、シークレットモード・学習を使わない設定では使わない（文脈で選ぶ）
TEST_F(ContextRerankRewriterTest, NotUsedWhenHistoryIsOff) {
  Correct("後悔", Request());
  ASSERT_EQ(Top("公開"), "公開");
  config_.set_incognito_mode(true);
  EXPECT_EQ(Top("公開"), "後悔");
  config_.set_incognito_mode(false);
  config_.set_history_learning_level(config::Config::NO_HISTORY);
  EXPECT_EQ(Top("公開"), "後悔");
}

// 開き直しても覚えている
TEST_F(ContextRerankRewriterTest, PersistsAcrossRestart) {
  Correct("後悔", Request());
  rewriter_ = std::make_unique<ContextRerankRewriter>();
  EXPECT_EQ(Top("公開"), "公開");
}

}  // namespace
}  // namespace mozc::context_rerank
