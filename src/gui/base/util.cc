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

#include "gui/base/util.h"

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"

// Show the build number on the title for debugging when the build
// configuration is official dev channel.
#if defined(CHANNEL_DEV) && defined(GOOGLE_JAPANESE_INPUT_BUILD)
#define MOZC_SHOW_BUILD_NUMBER_ON_TITLE
#endif  // CHANNEL_DEV && GOOGLE_JAPANESE_INPUT_BUILD

#include <QAbstractButton>
#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QObject>
#include <QPalette>
#include <QStyleHints>
#include <QStyleFactory>
#include <QtGui>
#include <memory>
#include <string>
#include <utility>

#ifdef MOZC_SHOW_BUILD_NUMBER_ON_TITLE
#include "gui/base/window_title_modifier.h"
#endif  // MOZC_SHOW_BUILD_NUMBER_ON_TITLE

namespace mozc {
namespace gui {

namespace {
void InstallEventFilter() {
#ifdef MOZC_SHOW_BUILD_NUMBER_ON_TITLE
  static WindowTitleModifier window_title_modifier;
  // Install WindowTitleModifier for official dev channel
  // append a special footer (Dev x.x.x) to the all Windows.
  qApp->installEventFilter(&window_title_modifier);
#endif  // MOZC_SHOW_BUILD_NUMBER_ON_TITLE
}

void InstallDefaultTranslator() {
  // qApplication must be loaded first
  CHECK(qApp);
  static absl::NoDestructor<QTranslator> translator;

  // Load "<translation_path>/qt_<lang>.qm" from a qrc file.
  bool loaded = translator->load(
      QLocale::system(), QLatin1String("qt"), QLatin1String("_"),
      QLibraryInfo::path(QLibraryInfo::TranslationsPath),
      QLatin1String(".qm"));
  if (loaded) {
    qApp->installTranslator(translator.get());
  } else {
    // Load ":/qt_<lang>.qm" from a qrc file.
    GuiUtil::InstallTranslator("qt");
  }

  // Load ":/tr_<lang>.qm" from a qrc file for the product name.
  GuiUtil::InstallTranslator("tr");
}
}  // namespace

// static
std::unique_ptr<QApplication> GuiUtil::InitQt(int &argc, char *argv[]) {
  QApplication::setStyle(QStyleFactory::create(QLatin1String("fusion")));

  // QApplication takes argc as a reference.
  auto app = std::make_unique<QApplication>(argc, argv);
  // IMi：Windows が暗い配色のときは、暗い配色の色をそろえて決める。Qt の Fusion の既定のままだと、
  // 表の行が紺と灰で交互になり、チェックしていない四角が青く塗られて見分けにくい
  if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
    const QColor accent = app->palette().color(QPalette::Highlight);
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x2b));
    pal.setColor(QPalette::WindowText, QColor(0xe8, 0xe8, 0xe8));
    pal.setColor(QPalette::Base, QColor(0x1f, 0x1f, 0x1f));
    pal.setColor(QPalette::AlternateBase, QColor(0x27, 0x27, 0x27));
    pal.setColor(QPalette::Text, QColor(0xe8, 0xe8, 0xe8));
    pal.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9a, 0x9a));
    pal.setColor(QPalette::Button, QColor(0x3a, 0x3a, 0x3a));
    pal.setColor(QPalette::ButtonText, QColor(0xe8, 0xe8, 0xe8));
    pal.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3a, 0x3a));
    pal.setColor(QPalette::ToolTipText, QColor(0xe8, 0xe8, 0xe8));
    pal.setColor(QPalette::Light, QColor(0x50, 0x50, 0x50));
    pal.setColor(QPalette::Midlight, QColor(0x44, 0x44, 0x44));
    pal.setColor(QPalette::Mid, QColor(0x33, 0x33, 0x33));
    pal.setColor(QPalette::Dark, QColor(0x1a, 0x1a, 0x1a));
    pal.setColor(QPalette::Shadow, QColor(0x10, 0x10, 0x10));
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::Link, QColor(0x8a, 0xb4, 0xf8));
    pal.setColor(QPalette::LinkVisited, QColor(0xc5, 0x8a, 0xf9));
    for (const QPalette::ColorRole role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
      pal.setColor(QPalette::Disabled, role, QColor(0x80, 0x80, 0x80));
    }
    pal.setColor(QPalette::Disabled, QPalette::Base, QColor(0x2b, 0x2b, 0x2b));
    pal.setColor(QPalette::Disabled, QPalette::Button, QColor(0x33, 0x33, 0x33));
    app->setPalette(pal);
  }
#ifdef __APPLE__
  app->setFont(QFont("Hiragino Sans"));
#endif  // __APPLE__

  InstallEventFilter();
  InstallDefaultTranslator();

  return app;
}

// static
void GuiUtil::InstallTranslator(absl::string_view resource_name) {
  static absl::NoDestructor<
      absl::flat_hash_map<std::string, std::unique_ptr<QTranslator>>>
      translators;
  if (translators->contains(resource_name)) {
    return;
  }
  auto translator = std::make_unique<QTranslator>();

  // Load ":/<resource_name>_<lang>.qm" from a qrc file.
  if (translator->load(
          QLocale::system(),
          QLatin1String(resource_name.data(), resource_name.size()),
          QLatin1String("_"), QLatin1String(":/"), QLatin1String(".qm"))) {
    qApp->installTranslator(translator.get());
    translators->emplace(resource_name, std::move(translator));
  }
}

// static
QString GuiUtil::ProductName() {
#ifdef GOOGLE_JAPANESE_INPUT_BUILD
  const QString name = QObject::tr("Google Japanese Input");
#else   // GOOGLE_JAPANESE_INPUT_BUILD
  const QString name = QStringLiteral("IMi");  // IMi
#endif  // GOOGLE_JAPANESE_INPUT_BUILD
  return name;
}

// static
void GuiUtil::ReplaceWidgetLabels(QWidget *widget) {
  ReplaceTitle(widget);
  for (auto *label : widget->findChildren<QLabel *>()) {
    ReplaceLabel(label);
  }
  for (auto *button : widget->findChildren<QAbstractButton *>()) {
    button->setText(ReplaceString(button->text()));
  }
}

// static
void GuiUtil::ReplaceLabel(QLabel *label) {
  label->setText(ReplaceString(label->text()));
}

// static
void GuiUtil::ReplaceTitle(QWidget *widget) {
  widget->setWindowTitle(GuiUtil::ReplaceString(widget->windowTitle()));
}

// static
QString GuiUtil::ReplaceString(const QString &str) {
  QString replaced(str);
  return replaced.replace(QLatin1String("[ProductName]"),
                          GuiUtil::ProductName());
}

}  // namespace gui
}  // namespace mozc
