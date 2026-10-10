// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Qt component of configure dialog for Mozc
#include "gui/config_dialog/config_dialog.h"
#include "renderer/imi_palette.h"

#include <QCheckBox>
#include <QFontMetricsF>
#include <QPainter>
#include <QSettings>
#include <QComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cstdint>
#include <istream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "base/config_file_stream.h"
#include "base/process.h"
#include "base/version.h"
#include "client/client.h"
#include "config/config_handler.h"
#include "gui/base/stats_config_util.h"
#include "gui/base/util.h"
#include "gui/config_dialog/keymap_editor.h"
#include "gui/config_dialog/roman_table_editor.h"
#include "protocol/config.pb.h"
#include "session/keymap.h"

#if defined(__ANDROID__) || defined(__wasm__)
#error "This platform is not supported."
#endif  // __ANDROID__ || __wasm__

#ifdef _WIN32
// clang-format off
#include <windows.h>
#include <QGuiApplication>
// clang-format on

#include "base/run_level.h"
#include "gui/base/win_util.h"
#include "win32/base/imm_util.h"
#endif  // _WIN32

#ifdef __APPLE__
#include "base/mac/mac_util.h"
#endif  // __APPLE__

namespace {
template <typename T>
void Connect(const QList<T*>& objects, const char* signal,
             const QObject* receiver, const char* slot) {
  for (typename QList<T*>::const_iterator itr = objects.begin();
       itr != objects.end(); ++itr) {
    QObject::connect(*itr, signal, receiver, slot);
  }
}
}  // namespace

namespace mozc {
namespace gui {

// Qt Style sheet for the config dialog.
// https://doc.qt.io/qt-6/stylesheet-reference.html
constexpr absl::string_view kQss = R"(
QFrame[class="setting-group-layout"] {
  padding: 0em 1em;
}
QFrame[class="setting-group-header"] {
}
QFrame[class="setting-group-line"] {
}
QListWidget#imiNav {
  background: transparent;
  border: none;
  border-right: 1px solid palette(mid);
  padding: 10px 8px;
  outline: 0;
}
QListWidget#imiNav::item {
  padding: 8px 10px;
  margin: 1px 0;
  border-radius: 6px;
}
QListWidget#imiNav::item:selected {
  background: palette(midlight);
  color: palette(window-text);
}
QListWidget#imiNav::item:hover:!selected {
  background: palette(alternate-base);
}
QFrame#imiCard {
  background: palette(alternate-base);
  border: 1px solid palette(mid);
  border-radius: 8px;
}
QScrollArea, QScrollArea > QWidget > QWidget {
  background: transparent;
}
)";

ConfigDialog::ConfigDialog()
    : client_(client::ClientFactory::NewClient()),
      initial_preedit_method_(0),
      initial_use_keyboard_to_change_preedit_method_(false),
      initial_use_mode_indicator_(true) {
  setupUi(this);
  setStyleSheet(QString::fromUtf8(kQss.data(), kQss.size()));
  SetupImiTab();

  // Remove the context help button (question mark button) from the window.
  Qt::WindowFlags flags = windowFlags();
  flags &= ~Qt::WindowContextHelpButtonHint;
  setWindowFlags(flags);

  setWindowModality(Qt::NonModal);

#ifdef _WIN32
  miscStartupWidget->setVisible(false);
#endif  // _WIN32

#ifdef __APPLE__
  miscDefaultIMEWidget->setVisible(false);
  miscAdministrationWidget->setVisible(false);
  setWindowTitle(tr("%1 Preferences").arg(GuiUtil::ProductName()));
#endif  // __APPLE__

#if defined(__linux__)
  miscDefaultIMEWidget->setVisible(false);
  miscAdministrationWidget->setVisible(false);
  miscStartupWidget->setVisible(false);
#endif  // __linux__

#ifdef NDEBUG
  // disable logging options
  miscLoggingWidget->setVisible(false);

#if defined(__linux__)
  // The last "misc" tab has no valid configs on Linux
  constexpr int kMiscTabIndex = 7;  // IMi：先頭に IMi のページを足したので 6 から 7 に
  configDialogTabWidget->removeTab(kMiscTabIndex);
#endif  // __linux__
#endif  // NDEBUG

  suggestionsSizeSpinBox->setRange(1, 9);

  punctuationsSettingComboBox->addItem(QString::fromUtf8("、。"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("，．"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("、．"));
  punctuationsSettingComboBox->addItem(QString::fromUtf8("，。"));

  symbolsSettingComboBox->addItem(QString::fromUtf8("「」・"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("[]／"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("「」／"));
  symbolsSettingComboBox->addItem(QString::fromUtf8("[]・"));

  keymapSettingComboBox->addItem(tr("Custom keymap"));
  keymapSettingComboBox->addItem(tr("ATOK"));
  keymapSettingComboBox->addItem(tr("MS-IME"));
  keymapSettingComboBox->addItem(tr("Kotoeri"));

  keymapname_sessionkeymap_map_[tr("ATOK")] = config::Config::ATOK;
  keymapname_sessionkeymap_map_[tr("MS-IME")] = config::Config::MSIME;
  keymapname_sessionkeymap_map_[tr("Kotoeri")] = config::Config::KOTOERI;

  inputModeComboBox->addItem(tr("Romaji"));
  inputModeComboBox->addItem(tr("Kana"));
#ifdef _WIN32
  // These options changing the preedit method by a hot key are only
  // supported by Windows.
  inputModeComboBox->addItem(tr("Romaji (switchable)"));
  inputModeComboBox->addItem(tr("Kana (switchable)"));
#endif  // _WIN32

  spaceCharacterFormComboBox->addItem(tr("Follow input mode"));
  spaceCharacterFormComboBox->addItem(tr("Fullwidth"));
  spaceCharacterFormComboBox->addItem(tr("Halfwidth"));

  selectionShortcutModeComboBox->addItem(tr("No shortcut"));
  selectionShortcutModeComboBox->addItem(tr("1 -- 9"));
  selectionShortcutModeComboBox->addItem(tr("A -- L"));

  historyLearningLevelComboBox->addItem(tr("Yes"));
  historyLearningLevelComboBox->addItem(tr("Yes (don't record new data)"));
  historyLearningLevelComboBox->addItem(tr("No"));

  shiftKeyModeSwitchComboBox->addItem(tr("Off"));
  shiftKeyModeSwitchComboBox->addItem(tr("Alphanumeric"));
  shiftKeyModeSwitchComboBox->addItem(tr("Katakana"));

  numpadCharacterFormComboBox->addItem(tr("Follow input mode"));
  numpadCharacterFormComboBox->addItem(tr("Fullwidth"));
  numpadCharacterFormComboBox->addItem(tr("Halfwidth"));
  numpadCharacterFormComboBox->addItem(tr("Direct input"));

  verboseLevelComboBox->addItem(tr("0"));
  verboseLevelComboBox->addItem(tr("1"));
  verboseLevelComboBox->addItem(tr("2"));

  yenSignComboBox->addItem(tr("Yen Sign ¥"));
  yenSignComboBox->addItem(tr("Backslash \\"));

#ifndef __APPLE__
  // On Windows/Linux, yenSignCombBox can be hidden.
  yenSignLabel->hide();
  yenSignComboBox->hide();
  // On Windows/Linux, useJapaneseLayout checkbox should be invisible.
  useJapaneseLayout->hide();
#endif  // !__APPLE__

#ifndef _WIN32
  // Mode indicator is available only on Windows.
  useModeIndicator->hide();
#endif  // !_WIN32

  // Reset texts explicitly for translations.
  configDialogButtonBox->button(QDialogButtonBox::Ok)->setText(tr("  Ok  "));
  configDialogButtonBox->button(QDialogButtonBox::Cancel)
      ->setText(tr("Cancel"));
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));

  // signal/slot
  QObject::connect(configDialogButtonBox, SIGNAL(clicked(QAbstractButton*)),
                   this, SLOT(clicked(QAbstractButton*)));
  QObject::connect(clearUserHistoryButton, SIGNAL(clicked()), this,
                   SLOT(ClearUserHistory()));
  QObject::connect(editUserDictionaryButton, SIGNAL(clicked()), this,
                   SLOT(EditUserDictionary()));
  QObject::connect(editKeymapButton, SIGNAL(clicked()), this,
                   SLOT(EditKeymap()));
  QObject::connect(resetToDefaultsButton, SIGNAL(clicked()), this,
                   SLOT(ResetToDefaults()));
  QObject::connect(editRomanTableButton, SIGNAL(clicked()), this,
                   SLOT(EditRomanTable()));
  QObject::connect(inputModeComboBox, SIGNAL(currentIndexChanged(int)), this,
                   SLOT(SelectInputModeSetting(int)));
  QObject::connect(useAutoConversion, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectAutoConversionSetting(int)));
  QObject::connect(historySuggestCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(dictionarySuggestCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(realtimeConversionCheckBox, SIGNAL(stateChanged(int)), this,
                   SLOT(SelectSuggestionSetting(int)));
  QObject::connect(launchAdministrationDialogButton, SIGNAL(clicked()), this,
                   SLOT(LaunchAdministrationDialog()));
  QObject::connect(launchAdministrationDialogButtonForUsageStats,
                   SIGNAL(clicked()), this, SLOT(LaunchAdministrationDialog()));

  // Event handlers to enable 'Apply' button.
  Connect(findChildren<QPushButton*>(), SIGNAL(clicked()), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QCheckBox*>(), SIGNAL(clicked()), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QComboBox*>(), SIGNAL(activated(int)), this,
          SLOT(EnableApplyButton()));
  Connect(findChildren<QSpinBox*>(), SIGNAL(editingFinished()), this,
          SLOT(EnableApplyButton()));
  // 'Apply' button is disabled on launching.
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

  // When clicking these messages, CheckBoxs corresponding
  // to them should be toggled.
  // We cannot use connect/slot as QLabel doesn't define
  // clicked slot by default.
  usageStatsMessage->installEventFilter(this);
  incognitoModeMessage->installEventFilter(this);

#ifndef _WIN32
  checkDefaultCheckBox->setVisible(false);
  checkDefaultLine->setVisible(false);
  checkDefaultLabel->setVisible(false);
#endif  // !_WIN32

#ifdef _WIN32
  launchAdministrationDialogButton->setEnabled(true);
  // if the current application is not elevated by UAC,
  // add a shield icon
  if (!mozc::RunLevel::IsElevatedByUAC()) {
    const QIcon& vista_shield_icon =
        QApplication::style()->standardIcon(QStyle::SP_VistaShield);
    launchAdministrationDialogButton->setIcon(vista_shield_icon);
    launchAdministrationDialogButtonForUsageStats->setIcon(vista_shield_icon);
  }

  usageStatsCheckBox->setDisabled(true);
  usageStatsCheckBox->setVisible(false);
  usageStatsMessage->setDisabled(true);
  usageStatsMessage->setVisible(false);
#else   // _WIN32
  launchAdministrationDialogButton->setEnabled(false);
  launchAdministrationDialogButton->setVisible(false);
  launchAdministrationDialogButtonForUsageStats->setEnabled(false);
  launchAdministrationDialogButtonForUsageStats->setVisible(false);
  administrationLine->setVisible(false);
  administrationLabel->setVisible(false);
  dictionaryPreloadingAndUACLabel->setVisible(false);
#endif  // _WIN32

#ifdef __linux__
  // On Linux, disable all fields for UsageStats
  usageStatsLabel->setEnabled(false);
  usageStatsLabel->setVisible(false);
  usageStatsLine->setEnabled(false);
  usageStatsLine->setVisible(false);
  usageStatsMessage->setEnabled(false);
  usageStatsMessage->setVisible(false);
  usageStatsCheckBox->setEnabled(false);
  usageStatsCheckBox->setVisible(false);
#endif  // __linux__

  GuiUtil::ReplaceWidgetLabels(this);

  Reload();

#ifdef _WIN32
  IMEHotKeyDisabledCheckBox->setChecked(WinUtil::GetIMEHotKeyDisabled());
#else   // _WIN32
  IMEHotKeyDisabledCheckBox->setVisible(false);
#endif  // _WIN32

#ifdef CHANNEL_DEV
  usageStatsCheckBox->setEnabled(false);
#endif  // CHANNEL_DEV
}

bool ConfigDialog::SetConfig(const config::Config& config) {
  if (!client_->CheckVersionOrRestartServer()) {
    LOG(ERROR) << "CheckVersionOrRestartServer failed";
    return false;
  }

  if (!client_->SetConfig(config)) {
    LOG(ERROR) << "SetConfig failed";
    return false;
  }

  return true;
}

bool ConfigDialog::GetConfig(config::Config* config) {
  if (!client_->CheckVersionOrRestartServer()) {
    LOG(ERROR) << "CheckVersionOrRestartServer failed";
    return false;
  }

  if (!client_->GetConfig(config)) {
    LOG(ERROR) << "GetConfig failed";
    return false;
  }

  return true;
}

void ConfigDialog::Reload() {
  config::Config config;
  if (!GetConfig(&config)) {
    QMessageBox::critical(this, windowTitle(),
                          tr("Failed to get current config values."));
  }
  ConvertFromProto(config);

  SelectAutoConversionSetting(static_cast<int>(config.use_auto_conversion()));

  initial_preedit_method_ = static_cast<int>(config.preedit_method());
  initial_use_keyboard_to_change_preedit_method_ =
      config.use_keyboard_to_change_preedit_method();
  initial_use_mode_indicator_ = config.use_mode_indicator();
}

bool ConfigDialog::Update() {
  config::Config config;
  ConvertToProto(&config);

  if (config.session_keymap() == config::Config::CUSTOM &&
      config.custom_keymap_table().empty()) {
    QMessageBox::warning(this, windowTitle(),
                         tr("The current custom keymap table is empty. "
                            "When custom keymap is selected, "
                            "you must customize it."));
    return false;
  }

#if defined(_WIN32)
  if ((initial_preedit_method_ != static_cast<int>(config.preedit_method())) ||
      (initial_use_keyboard_to_change_preedit_method_ !=
       config.use_keyboard_to_change_preedit_method())) {
    QMessageBox::information(this, windowTitle(),
                             tr("Romaji/Kana setting is enabled from"
                                " new applications."));
    initial_preedit_method_ = static_cast<int>(config.preedit_method());
    initial_use_keyboard_to_change_preedit_method_ =
        config.use_keyboard_to_change_preedit_method();
  }
#endif  // _WIN32

#ifdef _WIN32
  if (initial_use_mode_indicator_ != config.use_mode_indicator()) {
    QMessageBox::information(this, windowTitle(),
                             tr("Input mode indicator setting is enabled from"
                                " new applications."));
    initial_use_mode_indicator_ = config.use_mode_indicator();
  }
#endif  // _WIN32

  if (!SetConfig(config)) {
    QMessageBox::critical(this, windowTitle(), tr("Failed to update config"));
  }

#ifdef _WIN32
  if (!WinUtil::SetIMEHotKeyDisabled(IMEHotKeyDisabledCheckBox->isChecked())) {
    // Do not show any dialog here, since this operation will not fail
    // in almost all cases.
    // TODO(taku): better to show dialog?
    LOG(ERROR) << "Failed to update IME HotKey status";
  }
#endif  // _WIN32

#ifdef __APPLE__
  if (startupCheckBox->isChecked()) {
    if (!MacUtil::CheckPrelauncherLoginItemStatus()) {
      MacUtil::AddPrelauncherLoginItem();
    }
  } else {
    if (MacUtil::CheckPrelauncherLoginItemStatus()) {
      MacUtil::RemovePrelauncherLoginItem();
    }
  }
#endif  // __APPLE__

  return true;
}

void ConfigDialog::SetSendStatsCheckBox() {
  // On windows, usage_stats flag is managed by
  // administration_dialog. http://b/2889759
#ifndef _WIN32
  const bool val = StatsConfigUtil::IsEnabled();
  usageStatsCheckBox->setChecked(val);
#endif  // _WIN32
}

void ConfigDialog::GetSendStatsCheckBox() const {
  // On windows, usage_stats flag is managed by
  // administration_dialog. http://b/2889759
#ifndef _WIN32
  const bool val = usageStatsCheckBox->isChecked();
  StatsConfigUtil::SetEnabled(val);
#endif  // _WIN32
}

#define SET_COMBOBOX(combobox, enumname, field)                    \
  do {                                                             \
    (combobox)->setCurrentIndex(static_cast<int>(config.field())); \
  } while (0)

#define SET_CHECKBOX(checkbox, field)       \
  do {                                      \
    (checkbox)->setChecked(config.field()); \
  } while (0)

#define GET_COMBOBOX(combobox, enumname, field)                              \
  do {                                                                       \
    config->set_##field(                                                     \
        static_cast<config::Config_##enumname>((combobox)->currentIndex())); \
  } while (0)

#define GET_CHECKBOX(checkbox, field)             \
  do {                                            \
    config->set_##field((checkbox)->isChecked()); \
  } while (0)

namespace {
static constexpr int kPreeditMethodSize = 2;

void SetComboboxForPreeditMethod(const config::Config& config,
                                 QComboBox* combobox) {
  int index = static_cast<int>(config.preedit_method());
#ifdef _WIN32
  if (config.use_keyboard_to_change_preedit_method()) {
    index += kPreeditMethodSize;
  }
#endif  // _WIN32
  combobox->setCurrentIndex(index);
}

void GetComboboxForPreeditMethod(const QComboBox* combobox,
                                 config::Config* config) {
  int index = combobox->currentIndex();
  if (index >= kPreeditMethodSize) {
    // |use_keyboard_to_change_preedit_method| should be true and
    // |index| should be adjusted to smaller than kPreeditMethodSize.
    config->set_preedit_method(
        static_cast<config::Config_PreeditMethod>(index - kPreeditMethodSize));
    config->set_use_keyboard_to_change_preedit_method(true);
  } else {
    config->set_preedit_method(
        static_cast<config::Config_PreeditMethod>(index));
    config->set_use_keyboard_to_change_preedit_method(false);
  }
}
}  // namespace

// TODO(taku)
// Actually ConvertFromProto and ConvertToProto are almost the same.
// The difference only SET_ and GET_. We would like to unify the twos.
void ConfigDialog::ConvertFromProto(const config::Config& config) {
  base_config_ = config;
  // IMi のページ
  imiHenkanMuhenkanCheckBox_->setChecked(
      std::find(config.overlay_keymaps().begin(), config.overlay_keymaps().end(),
                config::Config::OVERLAY_HENKAN_MUHENKAN_TO_IME_ON_OFF) !=
      config.overlay_keymaps().end());
  imiLiveConversionCheckBox_->setChecked(config.imi_live_conversion());
  imiUseLmCheckBox_->setChecked(config.imi_use_lm());
  imiUseContextCheckBox_->setChecked(config.imi_use_context());
  imiShowCandidatesCheckBox_->setChecked(config.imi_show_candidates_on_convert());
  imiStyleComboBox_->setCurrentIndex(static_cast<int>(config.imi_window_style()));
  imiColorModeComboBox_->setCurrentIndex(static_cast<int>(config.imi_color_mode()));
  {
    const int font = config.imi_candidate_font().empty()
                         ? 0
                         : imiFontComboBox_->findText(QString::fromStdString(config.imi_candidate_font()));
    imiFontComboBox_->setCurrentIndex(font < 0 ? 0 : font);
  }
  imiFontSizeComboBox_->setCurrentIndex(static_cast<int>(config.imi_font_size()));
  imiShareInputModeCheckBox_->setChecked(config.imi_share_input_mode());
  UpdateImiPreview();
  // tab1
  SetComboboxForPreeditMethod(config, inputModeComboBox);
  SET_COMBOBOX(punctuationsSettingComboBox, PunctuationMethod,
               punctuation_method);
  SET_COMBOBOX(symbolsSettingComboBox, SymbolMethod, symbol_method);
  SET_COMBOBOX(spaceCharacterFormComboBox, FundamentalCharacterForm,
               space_character_form);
  SET_COMBOBOX(selectionShortcutModeComboBox, SelectionShortcut,
               selection_shortcut);
  SET_COMBOBOX(numpadCharacterFormComboBox, NumpadCharacterForm,
               numpad_character_form);
  SET_COMBOBOX(keymapSettingComboBox, SessionKeymap, session_keymap);

  custom_keymap_table_ = config.custom_keymap_table();
  custom_roman_table_ = config.custom_roman_table();

  // tab2
  SET_COMBOBOX(historyLearningLevelComboBox, HistoryLearningLevel,
               history_learning_level);
  SET_CHECKBOX(singleKanjiConversionCheckBox, use_single_kanji_conversion);
  SET_CHECKBOX(symbolConversionCheckBox, use_symbol_conversion);
  SET_CHECKBOX(emoticonConversionCheckBox, use_emoticon_conversion);
  SET_CHECKBOX(dateConversionCheckBox, use_date_conversion);
  SET_CHECKBOX(emojiConversionCheckBox, use_emoji_conversion);
  SET_CHECKBOX(numberConversionCheckBox, use_number_conversion);
  SET_CHECKBOX(calculatorCheckBox, use_calculator);
  SET_CHECKBOX(t13nConversionCheckBox, use_t13n_conversion);
  SET_CHECKBOX(zipcodeConversionCheckBox, use_zip_code_conversion);
  SET_CHECKBOX(spellingCorrectionCheckBox, use_spelling_correction);

  // InfoListConfig
  localUsageDictionaryCheckBox->setChecked(
      config.information_list_config().use_local_usage_dictionary());

  // tab3
  SET_CHECKBOX(autoSwitchCompositionMode, auto_switch_composition_mode);

  SET_CHECKBOX(useAutoConversion, use_auto_conversion);
  kutenCheckBox->setChecked(config.auto_conversion_key() &
                            config::Config::AUTO_CONVERSION_KUTEN);
  toutenCheckBox->setChecked(config.auto_conversion_key() &
                             config::Config::AUTO_CONVERSION_TOUTEN);
  questionMarkCheckBox->setChecked(
      config.auto_conversion_key() &
      config::Config::AUTO_CONVERSION_QUESTION_MARK);
  exclamationMarkCheckBox->setChecked(
      config.auto_conversion_key() &
      config::Config::AUTO_CONVERSION_EXCLAMATION_MARK);

  SET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  SET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  SET_CHECKBOX(useModeIndicator, use_mode_indicator);

  // tab4
  SET_CHECKBOX(historySuggestCheckBox, use_history_suggest);
  SET_CHECKBOX(dictionarySuggestCheckBox, use_dictionary_suggest);
  SET_CHECKBOX(realtimeConversionCheckBox, use_realtime_conversion);

  suggestionsSizeSpinBox->setValue(
      std::clamp<int>(config.suggestions_size(), 1, 9));

  // tab5
  SetSendStatsCheckBox();
  SET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  SET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  SET_COMBOBOX(verboseLevelComboBox, int, verbose_level);
  SET_CHECKBOX(checkDefaultCheckBox, check_default);
  SET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Load(config);

#ifdef __APPLE__
  startupCheckBox->setChecked(MacUtil::CheckPrelauncherLoginItemStatus());
#endif  // __APPLE__
}

void ConfigDialog::ConvertToProto(config::Config* config) const {
  *config = base_config_;
  // IMi のページ
  {
    std::vector<int> overlays;
    for (int k : config->overlay_keymaps()) {
      if (k != config::Config::OVERLAY_HENKAN_MUHENKAN_TO_IME_ON_OFF) {
        overlays.push_back(k);
      }
    }
    if (imiHenkanMuhenkanCheckBox_->isChecked()) {
      overlays.push_back(config::Config::OVERLAY_HENKAN_MUHENKAN_TO_IME_ON_OFF);
    }
    config->clear_overlay_keymaps();
    for (int k : overlays) {
      config->add_overlay_keymaps(static_cast<config::Config::SessionKeymap>(k));
    }
  }
  config->set_imi_live_conversion(imiLiveConversionCheckBox_->isChecked());
  config->set_imi_use_lm(imiUseLmCheckBox_->isChecked());
  config->set_imi_use_context(imiUseContextCheckBox_->isChecked());
  config->set_imi_show_candidates_on_convert(imiShowCandidatesCheckBox_->isChecked());
  config->set_imi_window_style(
      static_cast<config::Config::ImiWindowStyle>(imiStyleComboBox_->currentIndex()));
  config->set_imi_color_mode(
      static_cast<config::Config::ImiColorMode>(imiColorModeComboBox_->currentIndex()));
  config->set_imi_candidate_font(imiFontComboBox_->currentIndex() > 0
                                     ? imiFontComboBox_->currentText().toStdString()
                                     : std::string());
  config->set_imi_font_size(
      static_cast<config::Config::ImiFontSize>(imiFontSizeComboBox_->currentIndex()));
  config->set_imi_share_input_mode(imiShareInputModeCheckBox_->isChecked());
#ifdef _WIN32
  // IME の部品（TIP）は設定ファイルを読まないので、レジストリにも書く（tip_thread_context.cc）
  QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\IMi"), QSettings::NativeFormat)
      .setValue(QStringLiteral("ShareInputMode"), imiShareInputModeCheckBox_->isChecked() ? 1 : 0);
#endif  // _WIN32

  // tab1
  GetComboboxForPreeditMethod(inputModeComboBox, config);
  GET_COMBOBOX(punctuationsSettingComboBox, PunctuationMethod,
               punctuation_method);
  GET_COMBOBOX(symbolsSettingComboBox, SymbolMethod, symbol_method);
  GET_COMBOBOX(spaceCharacterFormComboBox, FundamentalCharacterForm,
               space_character_form);
  GET_COMBOBOX(selectionShortcutModeComboBox, SelectionShortcut,
               selection_shortcut);
  GET_COMBOBOX(numpadCharacterFormComboBox, NumpadCharacterForm,
               numpad_character_form);
  GET_COMBOBOX(keymapSettingComboBox, SessionKeymap, session_keymap);

  config->set_custom_keymap_table(custom_keymap_table_);

  config->clear_custom_roman_table();
  if (!custom_roman_table_.empty()) {
    config->set_custom_roman_table(custom_roman_table_);
  }

  // tab2
  GET_COMBOBOX(historyLearningLevelComboBox, HistoryLearningLevel,
               history_learning_level);
  GET_CHECKBOX(singleKanjiConversionCheckBox, use_single_kanji_conversion);
  GET_CHECKBOX(symbolConversionCheckBox, use_symbol_conversion);
  GET_CHECKBOX(emoticonConversionCheckBox, use_emoticon_conversion);
  GET_CHECKBOX(dateConversionCheckBox, use_date_conversion);
  GET_CHECKBOX(emojiConversionCheckBox, use_emoji_conversion);
  GET_CHECKBOX(numberConversionCheckBox, use_number_conversion);
  GET_CHECKBOX(calculatorCheckBox, use_calculator);
  GET_CHECKBOX(t13nConversionCheckBox, use_t13n_conversion);
  GET_CHECKBOX(zipcodeConversionCheckBox, use_zip_code_conversion);
  GET_CHECKBOX(spellingCorrectionCheckBox, use_spelling_correction);

  // InformationListConfig
  config->mutable_information_list_config()->set_use_local_usage_dictionary(
      localUsageDictionaryCheckBox->isChecked());

  // tab3
  GET_CHECKBOX(autoSwitchCompositionMode, auto_switch_composition_mode);

  GET_CHECKBOX(useAutoConversion, use_auto_conversion);

  GET_CHECKBOX(useJapaneseLayout, use_japanese_layout);

  GET_CHECKBOX(useModeIndicator, use_mode_indicator);

  uint32_t auto_conversion_key = 0;
  if (kutenCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_KUTEN;
  }
  if (toutenCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_TOUTEN;
  }
  if (questionMarkCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_QUESTION_MARK;
  }
  if (exclamationMarkCheckBox->isChecked()) {
    auto_conversion_key |= config::Config::AUTO_CONVERSION_EXCLAMATION_MARK;
  }
  config->set_auto_conversion_key(auto_conversion_key);

  GET_COMBOBOX(shiftKeyModeSwitchComboBox, ShiftKeyModeSwitch,
               shift_key_mode_switch);

  // tab4
  GET_CHECKBOX(historySuggestCheckBox, use_history_suggest);
  GET_CHECKBOX(dictionarySuggestCheckBox, use_dictionary_suggest);
  GET_CHECKBOX(realtimeConversionCheckBox, use_realtime_conversion);

  config->set_suggestions_size(
      static_cast<uint32_t>(suggestionsSizeSpinBox->value()));

  // tab5
  GetSendStatsCheckBox();
  GET_CHECKBOX(incognitoModeCheckBox, incognito_mode);
  GET_CHECKBOX(presentationModeCheckBox, presentation_mode);

  // tab6
  config->set_verbose_level(verboseLevelComboBox->currentIndex());
  GET_CHECKBOX(checkDefaultCheckBox, check_default);
  GET_COMBOBOX(yenSignComboBox, YenSignCharacter, yen_sign_character);

  characterFormEditor->Save(config);
}

#undef SET_COMBOBOX
#undef SET_CHECKBOX
#undef GET_COMBOBOX
#undef GET_CHECKBOX

void ConfigDialog::clicked(QAbstractButton* button) {
  switch (configDialogButtonBox->buttonRole(button)) {
    case QDialogButtonBox::AcceptRole:
      if (Update()) {
        QWidget::close();
      }
      break;
    case QDialogButtonBox::ApplyRole:
      // IMi：適用できたら「適用」を灰色に戻し、反映したことがわかるようにする
      // 「適用」ボタン自身も「押されたら適用を有効にする」につながっているので、その処理のあとに灰色にする
      if (Update()) {
        QTimer::singleShot(0, this, [this]() {
          configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
        });
      }
      break;
    case QDialogButtonBox::RejectRole:
      QWidget::close();
      break;
    default:
      break;
  }
}

void ConfigDialog::ClearUserHistory() {
  if (QMessageBox::Ok !=
      QMessageBox::question(
          this, windowTitle(), tr("Do you want to clear all history data?"),
          QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel)) {
    return;
  }

  client_->CheckVersionOrRestartServer();

  if (!client_->ClearUserHistory()) {
    QMessageBox::critical(this, windowTitle(),
                          tr("%1 Converter is not running. "
                             "Settings were not saved.")
                              .arg(GuiUtil::ProductName()));
  }
}

void ConfigDialog::EditUserDictionary() {
  client_->LaunchTool("dictionary_tool", "");
}

void ConfigDialog::EditKeymap() {
  std::string current_keymap_table = "";
  const QString keymap_name = keymapSettingComboBox->currentText();
  const std::map<QString, config::Config::SessionKeymap>::const_iterator itr =
      keymapname_sessionkeymap_map_.find(keymap_name);
  if (itr != keymapname_sessionkeymap_map_.end()) {
    // Load from predefined mapping file.
    const char* keymap_file =
        keymap::KeyMapManager::GetKeyMapFileName(itr->second);
    std::unique_ptr<std::istream> ifs(
        ConfigFileStream::LegacyOpen(keymap_file));
    CHECK(ifs.get() != nullptr);  // should never happen
    std::stringstream buffer;
    buffer << ifs->rdbuf();
    current_keymap_table = buffer.str();
  } else {
    current_keymap_table = custom_keymap_table_;
  }
  std::string output;
  if (gui::KeyMapEditorDialog::Show(this, current_keymap_table, &output)) {
    custom_keymap_table_ = output;
    // set keymapSettingComboBox to "Custom keymap"
    keymapSettingComboBox->setCurrentIndex(0);
  }
}

void ConfigDialog::EditRomanTable() {
  std::string output;
  if (gui::RomanTableEditorDialog::Show(this, custom_roman_table_, &output)) {
    custom_roman_table_ = output;
  }
}

void ConfigDialog::SelectInputModeSetting(int index) {
  // enable "EDIT" button if roman mode is selected
  editRomanTableButton->setEnabled((index == 0));
}

void ConfigDialog::SelectAutoConversionSetting(int state) {
  kutenCheckBox->setEnabled(static_cast<bool>(state));
  toutenCheckBox->setEnabled(static_cast<bool>(state));
  questionMarkCheckBox->setEnabled(static_cast<bool>(state));
  exclamationMarkCheckBox->setEnabled(static_cast<bool>(state));
}

void ConfigDialog::SelectSuggestionSetting(int state) {
  if (historySuggestCheckBox->isChecked() ||
      dictionarySuggestCheckBox->isChecked() ||
      realtimeConversionCheckBox->isChecked()) {
    presentationModeCheckBox->setEnabled(true);
  } else {
    presentationModeCheckBox->setEnabled(false);
  }
}

void ConfigDialog::ResetToDefaults() {
  const QString message =
      tr("When you reset %1 settings, any changes "
         "you've made will be reverted to the default settings. "
         "Do you want to reset settings? "
         "The following items are not reset with this operation.\n"
         " - Personalization data\n"
         " - Input history\n"
         " - Usage statistics and crash reports\n"
         " - Administrator settings")
          .arg(GuiUtil::ProductName());
  if (QMessageBox::Ok ==
      QMessageBox::question(this, windowTitle(), message,
                            QMessageBox::Ok | QMessageBox::Cancel,
                            QMessageBox::Cancel)) {
    // TODO(taku): remove the dependency to config::ConfigHandler
    // nice to have GET_DEFAULT_CONFIG command
    ConvertFromProto(config::ConfigHandler::DefaultConfig());
  }
}

void ConfigDialog::LaunchAdministrationDialog() {
#ifdef _WIN32
  client_->LaunchTool("administration_dialog", "");
#endif  // _WIN32
}

namespace {

// IMi：見た目のページの見本。候補の窓と意味の窓を、選んでいるスタイル・配色・字体・大きさで小さく描く
class ImiPreviewWidget : public QWidget {
 public:
  explicit ImiPreviewWidget(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumHeight(158);
  }
  void Set(int style, bool dark, const QStringList& families, double scale) {
    style_ = style;
    dark_ = dark;
    families_ = families;
    scale_ = scale;
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    const renderer::ImiPalette p = renderer::GetImiPalette(style_, dark_);
    const auto c = [](uint32_t v) { return QColor((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); };
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    QFont base = font();
    base.setFamilies(families_);
    const auto sized = [&](double px, bool bold = false) {
      QFont f = base;
      f.setPixelSize(static_cast<int>(px * scale_));
      f.setBold(bold);
      return f;
    };
    QFont sans = font();
    const auto sans_sized = [&](double px) {
      QFont f = sans;
      f.setPixelSize(static_cast<int>(px * scale_));
      return f;
    };
    const int row_h = static_cast<int>(30 * scale_);
    const int pad = 6;
    // 候補の窓
    const QStringList cands = {QString::fromUtf8("言って"), QString::fromUtf8("行って"),
                               QString::fromUtf8("いって")};
    const int cw = static_cast<int>(150 * scale_);
    const int ch = pad * 2 + row_h * cands.size() + static_cast<int>(22 * scale_);
    const QRectF cand(4, 4, cw, ch);
    g.setPen(QPen(c(p.border), 1));
    g.setBrush(c(p.window_bg));
    g.drawRoundedRect(cand, 8, 8);
    for (int i = 0; i < cands.size(); ++i) {
      const QRectF row(cand.left() + pad, cand.top() + pad + i * row_h, cw - pad * 2, row_h);
      if (i == 0) {
        g.setPen(Qt::NoPen);
        g.setBrush(c(p.focus_bg));
        g.drawRoundedRect(row.adjusted(0, 1, 0, -1), 5, 5);
      }
      g.setFont(sans_sized(12));
      g.setPen(i == 0 ? c(p.accent) : c(p.sub_text));
      g.drawText(QRectF(row.left(), row.top(), 22 * scale_, row.height()), Qt::AlignCenter,
                 QString::number(i + 1));
      g.setFont(sized(16, i == 0));
      g.setPen(c(p.text));
      g.drawText(row.adjusted(26 * scale_, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, cands[i]);
    }
    const double fy = cand.top() + pad + row_h * cands.size();
    g.setPen(QPen(c(p.separator), 1));
    g.drawLine(QPointF(cand.left() + pad, fy + 2), QPointF(cand.right() - pad, fy + 2));
    g.setFont(sans_sized(10));
    g.setPen(c(p.sub_text));
    g.drawText(QRectF(cand.left() + pad, fy + 3, cw - pad * 2, 20 * scale_),
               Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("1 / 9"));
    // 意味の窓
    const double mx = cand.right() + 6;
    const double mw = std::max(120.0, width() - mx - 4);
    const QRectF mean(mx, 4, mw, std::max<double>(ch, 140 * scale_));
    g.setPen(QPen(c(p.border), 1));
    g.setBrush(c(p.window_bg));
    g.drawRoundedRect(mean, 8, 8);
    double y = mean.top() + 10;
    g.setFont(sized(19));
    g.setPen(c(p.text));
    const QString word = QString::fromUtf8("言う");
    const double word_w = QFontMetricsF(g.font()).horizontalAdvance(word);
    const double word_h = QFontMetricsF(g.font()).height();
    g.drawText(QPointF(mean.left() + 14, y + QFontMetricsF(g.font()).ascent()), word);
    g.setFont(sans_sized(11));
    g.setPen(c(p.sub_text));
    g.drawText(QPointF(mean.left() + 14 + word_w + 8, y + word_h - 6), QString::fromUtf8("いう・動詞"));
    y += word_h + 6;
    g.setPen(QPen(c(p.separator), 1));
    g.drawLine(QPointF(mean.left() + 14, y), QPointF(mean.right() - 14, y));
    y += 8;
    const QStringList senses = {QString::fromUtf8("言葉に出す。"),
                                QString::fromUtf8("表現する。伝達する。主張する。")};
    g.setFont(sized(12));
    const double line_h = QFontMetricsF(g.font()).height() + 3;
    for (int i = 0; i < senses.size(); ++i) {
      g.setPen(c(p.accent));
      g.drawText(QPointF(mean.left() + 14, y + QFontMetricsF(g.font()).ascent()), QString::number(i + 1));
      g.setPen(c(p.text));
      g.drawText(QRectF(mean.left() + 30, y, mean.width() - 44, line_h),
                 Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine,
                 QFontMetricsF(g.font()).elidedText(senses[i], Qt::ElideRight, mean.width() - 44));
      y += line_h;
    }
    g.setFont(sans_sized(10));
    g.setPen(c(p.sub_text));
    g.drawText(QPointF(mean.left() + 14, y + 6 + QFontMetricsF(g.font()).ascent()),
               QString::fromUtf8("ウィクショナリー日本語版より"));
  }

 private:
  int style_ = 0;
  bool dark_ = false;
  QStringList families_;
  double scale_ = 1.0;
};

// Windows のアプリの配色が暗いか
bool WindowsAppsDark() {
#ifdef _WIN32
  QSettings s(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
              QSettings::NativeFormat);
  return s.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#else
  return false;
#endif
}

}  // namespace

// IMi：設定画面の作り直し（2026-10-07）。タブをやめて、左に項目の一覧、右にページを置く。
// .ui で作った部品（設定とのつなぎは ConvertFromProto・ConvertToProto のまま）を新しいページへ移し、
// 元のタブは隠す。移さない部品（使用統計など）は隠したタブに残り、値はそのまま保たれる。
void ConfigDialog::SetupImiTab() {
  QWidget* host = new QWidget(this);
  QHBoxLayout* host_layout = new QHBoxLayout(host);
  host_layout->setContentsMargins(0, 0, 0, 0);
  host_layout->setSpacing(0);
  QListWidget* nav = new QListWidget(host);
  nav->setObjectName(QStringLiteral("imiNav"));
  nav->setFixedWidth(176);
  nav->setFrameShape(QFrame::NoFrame);
  QStackedWidget* stack = new QStackedWidget(host);
  host_layout->addWidget(nav);
  host_layout->addWidget(stack, 1);
  if (QLayout* parent_layout = configDialogTabWidget->parentWidget()->layout()) {
    parent_layout->replaceWidget(configDialogTabWidget, host);
  }
  configDialogTabWidget->hide();
  QObject::connect(nav, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
  // 選択だけが変わったとき（支援技術などからの選択）もページを切り替える
  QObject::connect(nav, &QListWidget::itemSelectionChanged, stack, [nav, stack]() {
    const QList<QListWidgetItem*> sel = nav->selectedItems();
    if (!sel.isEmpty()) stack->setCurrentIndex(nav->row(sel.first()));
  });

  const auto dim = [](QLabel* label) {
    QPalette pal = label->palette();
    QColor c = pal.color(QPalette::WindowText);
    c.setAlpha(165);
    pal.setColor(QPalette::WindowText, c);
    label->setPalette(pal);
  };
  // ページ：題名と、カードを縦に並べる場所
  const auto add_page = [&](const char* name) {
    nav->addItem(QString::fromUtf8(name));
    QScrollArea* area = new QScrollArea(stack);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    QWidget* content = new QWidget(area);
    QVBoxLayout* v = new QVBoxLayout(content);
    v->setContentsMargins(20, 14, 20, 14);
    v->setSpacing(12);
    QLabel* title = new QLabel(QString::fromUtf8(name), content);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.45);
    f.setBold(true);
    title->setFont(f);
    v->addWidget(title);
    area->setWidget(content);
    stack->addWidget(area);
    return v;
  };
  // カード：設定の行をまとめる枠
  const auto add_card = [&](QVBoxLayout* page, const char* heading = nullptr) {
    if (heading != nullptr) {
      QLabel* h = new QLabel(QString::fromUtf8(heading));
      dim(h);
      page->addWidget(h);
    }
    QFrame* card = new QFrame();
    card->setObjectName(QStringLiteral("imiCard"));
    QVBoxLayout* v = new QVBoxLayout(card);
    v->setContentsMargins(16, 10, 16, 10);
    v->setSpacing(10);
    page->addWidget(card);
    return v;
  };
  // 行：左に名前と説明、右に部品
  const auto add_row = [&](QVBoxLayout* card, const char* name, const char* note, QWidget* w) {
    QHBoxLayout* row = new QHBoxLayout();
    QVBoxLayout* text = new QVBoxLayout();
    text->setSpacing(2);
    QLabel* n = new QLabel(QString::fromUtf8(name));
    n->setWordWrap(true);
    text->addWidget(n);
    if (note != nullptr) {
      QLabel* d = new QLabel(QString::fromUtf8(note));
      d->setWordWrap(true);
      dim(d);
      text->addWidget(d);
    }
    if (w != nullptr) n->setBuddy(w);
    row->addLayout(text, 1);
    if (w != nullptr) {
      w->setMinimumWidth(std::max(w->minimumWidth(), 190));
      row->addWidget(w, 0, Qt::AlignVCenter);
    }
    card->addLayout(row);
  };
  // チェックボックスの行：チェックボックスの文字を名前にし、説明を下に
  const auto add_check = [&](QVBoxLayout* card, QCheckBox* box, const char* name, const char* note) {
    box->setText(QString::fromUtf8(name));
    QVBoxLayout* v = new QVBoxLayout();
    v->setSpacing(2);
    v->addWidget(box);
    if (note != nullptr) {
      QLabel* d = new QLabel(QString::fromUtf8(note));
      d->setWordWrap(true);
      d->setContentsMargins(26, 0, 0, 0);
      dim(d);
      v->addWidget(d);
    }
    card->addLayout(v);
  };

  imiHenkanMuhenkanCheckBox_ = new QCheckBox(this);
  imiLiveConversionCheckBox_ = new QCheckBox(this);
  imiUseLmCheckBox_ = new QCheckBox(this);
  imiUseContextCheckBox_ = new QCheckBox(this);
  imiShowCandidatesCheckBox_ = new QCheckBox(this);
  imiShareInputModeCheckBox_ = new QCheckBox(this);

  // 基本
  {
    QVBoxLayout* page = add_page("基本");
    QVBoxLayout* c1 = add_card(page);
    add_check(c1, imiLiveConversionCheckBox_, "入力に合わせて変換する（同時変換）",
              "オフにすると、Space キーを押すまで仮名のまま表示します。");
    add_check(c1, imiUseLmCheckBox_, "前後の文脈で漢字を選び直す",
              "小さな言語モデルで文全体の自然さを比べます。オフにすると軽くなりますが、精度は少し下がります。");
    add_check(c1, imiUseContextCheckBox_, "前に確定した文も手がかりにする",
              "直前に書いた文の内容から、続く語の漢字を選びます。");
    add_check(c1, imiShowCandidatesCheckBox_, "候補の横に語の意味を出す",
              "1回目の Space キーから候補の一覧を出し、選んでいる語の意味を表示します。");
    QVBoxLayout* c2 = add_card(page);
    add_check(c2, imiHenkanMuhenkanCheckBox_, "無変換キーで英数、変換キーでひらがな",
              "入力していないときの無変換キーは IME をオフにします。");
    add_row(c2, "句読点", nullptr, punctuationsSettingComboBox);
    add_row(c2, "記号", nullptr, symbolsSettingComboBox);
    QVBoxLayout* c3 = add_card(page);
    QPushButton* set_default = new QPushButton(QString::fromUtf8("既定にする"));
    QObject::connect(set_default, SIGNAL(clicked()), this, SLOT(SetImiDefault()));
    add_row(c3, "IMi をいつも使う入力方式にする",
            "新しく開いた窓や、PC を起動したときに IMi で始まるようにします。", set_default);
    // IMi：新しい版の確認。IMi 自身は通信せず、ブラウザでリリースのページを開くだけ
    QVBoxLayout* c4 = add_card(page);
    QPushButton* check_update = new QPushButton(QString::fromUtf8("新しい版を確かめる"));
    QObject::connect(check_update, SIGNAL(clicked()), this, SLOT(OpenImiReleases()));
    std::string version = Version::GetMozcVersion();
    if (absl::EndsWith(version, ".100")) version.resize(version.size() - 4);  // 開発版の REVISION は除く
    const std::string version_label = "今の版：" + version;
    add_row(c4, version_label.c_str(),
            "ブラウザで IMi の最新のリリースのページを開きます。版の番号が今の版より新しければ、"
            "そこからインストーラー（IMi_ で始まる .msi のファイル）をダウンロードして入れ直してください。",
            check_update);
    page->addStretch();
  }
  // 入力のしかた
  {
    QVBoxLayout* page = add_page("入力のしかた");
    QVBoxLayout* c1 = add_card(page);
    add_row(c1, "ローマ字入力・かな入力", nullptr, inputModeComboBox);
    add_row(c1, "スペースの入力", nullptr, spaceCharacterFormComboBox);
    add_row(c1, "テンキーからの入力", nullptr, numpadCharacterFormComboBox);
#ifdef __APPLE__
    add_row(c1, "¥ キー・バックスラッシュ キーからの入力", nullptr, yenSignComboBox);
#endif  // __APPLE__
    add_row(c1, "Shift キーでの切り替え", nullptr, shiftKeyModeSwitchComboBox);
    QVBoxLayout* c2 = add_card(page);
    add_check(c2, imiShareInputModeCheckBox_, "入力モード（あ・A）をすべてのアプリで共通にする",
              "オフにすると、アプリごとに最後の入力モードを覚えます。変えたあと、アプリを開き直すと反映されます。");
    add_check(c2, autoSwitchCompositionMode, "英数字が続いたら自動で半角英数にする", nullptr);
    add_check(c2, useJapaneseLayout, "日本語の入力ではいつも日本語のキー配列を使う", nullptr);
    add_check(c2, useAutoConversion, "句読点を打ったら変換する", "変換のきっかけにする記号を選べます。");
    QHBoxLayout* marks = new QHBoxLayout();
    marks->setContentsMargins(26, 0, 0, 0);
    for (QCheckBox* b : {kutenCheckBox, toutenCheckBox, questionMarkCheckBox, exclamationMarkCheckBox}) {
      marks->addWidget(b);
    }
    marks->addStretch();
    c2->addLayout(marks);
    QVBoxLayout* c3 = add_card(page);
    QWidget* keymap = new QWidget();
    QHBoxLayout* km = new QHBoxLayout(keymap);
    km->setContentsMargins(0, 0, 0, 0);
    km->addWidget(keymapSettingComboBox);
    km->addWidget(editKeymapButton);
    add_row(c3, "キーの割り当て", nullptr, keymap);
    add_row(c3, "ローマ字の表", nullptr, editRomanTableButton);
    page->addStretch();
  }
  // 変換と候補
  {
    QVBoxLayout* page = add_page("変換と候補");
    QVBoxLayout* c1 = add_card(page);
    add_row(c1, "候補を数字キーで選ぶ", nullptr, selectionShortcutModeComboBox);
    QVBoxLayout* c2 = add_card(page, "予測候補（打っている途中に出る候補）");
    add_check(c2, historySuggestCheckBox, "入力の履歴から出す", nullptr);
    add_check(c2, dictionarySuggestCheckBox, "辞書から出す", nullptr);
    add_check(c2, realtimeConversionCheckBox, "予測候補に変換結果も出す", nullptr);
    add_row(c2, "予測候補の数", nullptr, suggestionsSizeSpinBox);
    QVBoxLayout* c3 = add_card(page, "特別な変換");
    QGridLayout* grid = new QGridLayout();
    const std::pair<QCheckBox*, const char*> specials[] = {
        {singleKanjiConversionCheckBox, "漢字1文字"}, {emojiConversionCheckBox, "絵文字"},
        {symbolConversionCheckBox, "記号"}, {dateConversionCheckBox, "日付・時刻"},
        {emoticonConversionCheckBox, "顔文字"}, {numberConversionCheckBox, "数字の書き方"},
        {t13nConversionCheckBox, "カタカナ語を英語に"}, {calculatorCheckBox, "計算（1+2= など）"},
        {zipcodeConversionCheckBox, "郵便番号から住所"}, {spellingCorrectionCheckBox, "打ち間違いの候補"}};
    for (int k = 0; k < static_cast<int>(std::size(specials)); ++k) {
      specials[k].first->setText(QString::fromUtf8(specials[k].second));
      grid->addWidget(specials[k].first, k / 2, k % 2);
    }
    c3->addLayout(grid);
    page->addStretch();
  }
  // 見た目
  {
    QVBoxLayout* page = add_page("見た目");
    QVBoxLayout* c1 = add_card(page);
    imiStyleComboBox_ = new QComboBox();
    imiStyleComboBox_->addItems({QString::fromUtf8("和紙と墨"), QString::fromUtf8("すっきり"),
                                 QString::fromUtf8("夜")});
    add_row(c1, "スタイル", "候補の窓と意味の窓の見た目です。", imiStyleComboBox_);
    imiColorModeComboBox_ = new QComboBox();
    imiColorModeComboBox_->addItems({QString::fromUtf8("Windows に合わせる"),
                                     QString::fromUtf8("明るい"), QString::fromUtf8("暗い")});
    add_row(c1, "配色",
            "「Windows に合わせる」なら、Windows の明るい・暗いの設定に従います。スタイルが「夜」のときはいつも暗くなります。",
            imiColorModeComboBox_);
    imiFontComboBox_ = new QComboBox();
    imiFontComboBox_->addItem(QString::fromUtf8("スタイルの既定"));
    imiFontComboBox_->addItems(QFontDatabase::families(QFontDatabase::Japanese));
    imiFontComboBox_->setMaxVisibleItems(16);
    add_row(c1, "候補の字体", "この PC に入っている日本語のフォントから選べます。", imiFontComboBox_);
    imiFontSizeComboBox_ = new QComboBox();
    imiFontSizeComboBox_->addItems({QString::fromUtf8("ふつう"), QString::fromUtf8("小さい"),
                                    QString::fromUtf8("大きい")});
    add_row(c1, "文字の大きさ", nullptr, imiFontSizeComboBox_);
    QVBoxLayout* c2 = add_card(page, "見本");
    imiPreview_ = new ImiPreviewWidget();
    c2->addWidget(imiPreview_);
    QVBoxLayout* c3 = add_card(page);
    add_check(c3, useModeIndicator, "カーソルの近くに入力モード（あ・A）を出す", nullptr);
    for (QComboBox* c : {imiStyleComboBox_, imiColorModeComboBox_, imiFontComboBox_, imiFontSizeComboBox_}) {
      QObject::connect(c, SIGNAL(currentIndexChanged(int)), this, SLOT(UpdateImiPreview()));
    }
    page->addStretch();
  }
  // 辞書と学習
  {
    QVBoxLayout* page = add_page("辞書と学習");
    QVBoxLayout* c1 = add_card(page);
    add_row(c1, "よく使う変換を覚える", "選んだ候補を覚えて、次から先に出します。", historyLearningLevelComboBox);
    clearUserHistoryButton->setText(QString::fromUtf8("覚えた内容を消す"));
    add_row(c1, "覚えた内容を消す", nullptr, clearUserHistoryButton);
    QVBoxLayout* c2 = add_card(page);
    editUserDictionaryButton->setText(QString::fromUtf8("辞書ツールを開く"));
    add_row(c2, "自分用の単語を登録する", "人名や専門用語など、辞書にない語を登録できます。",
            editUserDictionaryButton);
    localUsageDictionaryCheckBox->hide();
    page->addStretch();
  }
  // プライバシー
  {
    QVBoxLayout* page = add_page("プライバシー");
    QVBoxLayout* c1 = add_card(page);
    add_check(c1, incognitoModeCheckBox, "シークレットモード",
              "しばらくのあいだ、変換を覚える・履歴から予測する・自分用の単語を使う、をやめます。");
    add_check(c1, presentationModeCheckBox, "プレゼンテーションモード",
              "しばらくのあいだ、予測候補を出しません。");
    QLabel* note = new QLabel(QString::fromUtf8(
        "IMi は入力した文字をインターネットに送りません。変換はすべてこの PC の中で行います。"));
    note->setWordWrap(true);
    dim(note);
    page->addWidget(note);
    page->addStretch();
  }
  // 詳しい設定
  {
    QVBoxLayout* page = add_page("詳しい設定");
    QVBoxLayout* c1 = add_card(page, "半角・全角");
    c1->addWidget(characterFormEditor);
    characterFormEditor->setMinimumHeight(260);
    QVBoxLayout* c2 = add_card(page);
    add_check(c2, checkDefaultCheckBox, "起動のとき、IMi が既定の入力方式かを確かめる", nullptr);
    add_check(c2, IMEHotKeyDisabledCheckBox, "Ctrl+Shift での入力方式の切り替えを使わない", nullptr);
    launchAdministrationDialogButton->setText(QString::fromUtf8("設定を開く"));
    add_row(c2, "辞書の先読みと管理者の設定", "管理者の許可（UAC）が要ります。",
            launchAdministrationDialogButton);
    page->addStretch();
  }
  nav->setCurrentRow(0);
  resize(std::max(width(), 760), std::max(height(), 600));
}

void ConfigDialog::OpenImiReleases() {
  Process::OpenBrowser("https://github.com/iori3mk/imi-ime/releases/latest");
}

void ConfigDialog::SetImiDefault() {
#ifdef _WIN32
  const bool ok = win32::ImeUtil::SetDefault();
  QMessageBox::information(
      this, windowTitle(),
      QString::fromUtf8(ok ? "IMi を既定の入力方式にしました。"
                           : "既定にできませんでした。Windows の設定の「時刻と言語」→「入力」→"
                             "「キーボードの詳細設定」から選んでください。"));
#endif  // _WIN32
}

void ConfigDialog::UpdateImiPreview() {
  if (imiPreview_ == nullptr) return;
  const int style = imiStyleComboBox_->currentIndex();
  const int color = imiColorModeComboBox_->currentIndex();
  const bool dark = color == 2 || (color == 0 && WindowsAppsDark());
  const renderer::ImiPalette p = renderer::GetImiPalette(style, dark);
  QStringList families;
  if (imiFontComboBox_->currentIndex() > 0) families << imiFontComboBox_->currentText();
  if (p.serif) {
    families << QStringLiteral("Noto Serif JP") << QStringLiteral("Yu Mincho");
  } else {
    families << QStringLiteral("BIZ UDPGothic") << QStringLiteral("Noto Sans JP")
             << QStringLiteral("Yu Gothic UI");
  }
  const double scale = imiFontSizeComboBox_->currentIndex() == 1   ? 0.88
                       : imiFontSizeComboBox_->currentIndex() == 2 ? 1.15
                                                                    : 1.0;
  static_cast<ImiPreviewWidget*>(imiPreview_)->Set(style, dark, families, scale);
}

void ConfigDialog::OpenDictionaryTool() { EditUserDictionary(); }

void ConfigDialog::EnableApplyButton() {
  configDialogButtonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

// Catch MouseButtonRelease event to toggle the CheckBoxes
bool ConfigDialog::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::MouseButtonRelease) {
    if (obj == usageStatsMessage) {
#ifndef CHANNEL_DEV
      usageStatsCheckBox->toggle();
#endif  // CHANNEL_DEV
    } else if (obj == incognitoModeMessage) {
      incognitoModeCheckBox->toggle();
    }
  }
  return QObject::eventFilter(obj, event);
}

}  // namespace gui
}  // namespace mozc
