// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QBuffer>
#include <QByteArray>
#include <QGraphicsOpacityEffect>
#include <QIODevice>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStyleOption>
#include <QVBoxLayout>
#include "common/container/unordered_map.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "ui_loading_screen.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "yuzu/loading_screen.h"

// Mingw seems to not have QMovie at all. If QMovie is missing then use a single frame instead of an
// showing the full animation
#if !YUZU_QT_MOVIE_MISSING
#include <QMovie>
#endif

// The screen sits where the game will appear, so it is nearly black; the type and the accent
// follow the Giuray theme.
constexpr char SCREEN_STYLE[] = R"(
QWidget#LoadingScreen, QWidget#fade_parent {
  background-color: #0A0A0B;
}
QLabel {
  background: transparent;
  color: #FAFAFA;
}
QLabel#game_label {
  color: #A1A1AA;
  font-size: 13px;
}
QLabel#stage {
  color: #FAFAFA;
  font-size: 24px;
  font-weight: 600;
}
QLabel#value {
  color: #A1A1AA;
  font-size: 13px;
}
QPushButton {
  background-color: #202024;
  color: #E4E4E7;
  border: 1px solid #38383F;
  border-radius: 6px;
  padding: 6px 24px;
}
QPushButton:hover {
  background-color: #29292E;
  border-color: #52525B;
}
QPushButton:disabled {
  color: #52525B;
  border-color: #202024;
})";

constexpr char PROGRESSBAR_STYLE_PREPARE[] = R"(
QProgressBar {}
QProgressBar::chunk {})";

constexpr char PROGRESSBAR_STYLE_BUILD[] = R"(
QProgressBar {
  background-color: #202024;
  border: none;
  border-radius: 3px;
}
QProgressBar::chunk {
  background-color: #DC2626;
  border-radius: 3px;
})";

constexpr char PROGRESSBAR_STYLE_COMPLETE[] = R"(
QProgressBar {
  background-color: #202024;
  border: none;
  border-radius: 3px;
}
QProgressBar::chunk {
  background-color: #DC2626;
  border-radius: 3px;
})";

constexpr int ProgressWidth = 480;

LoadingScreen::LoadingScreen(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::LoadingScreen>()),
      previous_stage(VideoCore::LoadCallbackStage::Complete) {
    ui->setupUi(this);
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);

    // Create a fade out effect to hide this loading screen widget.
    // When fading opacity, it will fade to the parent widgets background color, which is why we
    // create an internal widget named fade_widget that we use the effect on, while keeping the
    // loading screen widget's background color black. This way we can create a fade to black effect
    opacity_effect = new QGraphicsOpacityEffect(this);
    opacity_effect->setOpacity(1);
    ui->fade_parent->setGraphicsEffect(opacity_effect);
    fadeout_animation = std::make_unique<QPropertyAnimation>(opacity_effect, "opacity");
    fadeout_animation->setDuration(500);
    fadeout_animation->setStartValue(1);
    fadeout_animation->setEndValue(0);
    fadeout_animation->setEasingCurve(QEasingCurve::OutBack);

    // After the fade completes, hide the widget and reset the opacity
    connect(fadeout_animation.get(), &QPropertyAnimation::finished, [this] {
        hide();
        opacity_effect->setOpacity(1);
        emit Hidden();
    });
    connect(this, &LoadingScreen::LoadProgress, this, &LoadingScreen::OnLoadProgress,
            Qt::QueuedConnection);

    // Shown while shaders are precached: starts the game without waiting for the rest.
    skip_button = new QPushButton(tr("Skip"), this);
    skip_button->setToolTip(tr("Start the game now. The shaders not precached yet are precached "
                               "on a later launch."));
    skip_button->hide();
    connect(skip_button, &QPushButton::clicked, this, [this] {
        VideoCommon::ShaderWarm::RequestSkipWarm();
        skip_button->setEnabled(false);
    });
    qRegisterMetaType<VideoCore::LoadCallbackStage>();

    // The game's name, then what is happening, a thin bar, and the details under it.
    setStyleSheet(QString::fromUtf8(SCREEN_STYLE));
    for (QWidget* widget : {static_cast<QWidget*>(ui->stage), static_cast<QWidget*>(ui->value),
                            static_cast<QWidget*>(ui->banner),
                            static_cast<QWidget*>(ui->progress_bar)}) {
        widget->setStyleSheet({});
    }
    game_label = new QLabel(this);
    game_label->setObjectName(QStringLiteral("game_label"));
    ui->progress_bar->setMinimumSize(ProgressWidth, 6);
    ui->progress_bar->setMaximumSize(ProgressWidth, 6);
    ui->logo->setMargin(32);
    ui->banner->setMargin(32);
    QVBoxLayout* center = ui->verticalLayout;
    while (QLayoutItem* item = center->takeAt(0)) {
        delete item;
    }
    center->setSpacing(0);
    center->addStretch(1);
    center->addWidget(game_label, 0, Qt::AlignHCenter);
    center->addSpacing(8);
    center->addWidget(ui->stage, 0, Qt::AlignHCenter);
    center->addSpacing(24);
    center->addWidget(ui->progress_bar, 0, Qt::AlignHCenter);
    center->addSpacing(16);
    center->addWidget(ui->value, 0, Qt::AlignHCenter);
    center->addSpacing(24);
    center->addWidget(skip_button, 0, Qt::AlignHCenter);
    center->addStretch(1);

    stage_translations = {
        {VideoCore::LoadCallbackStage::Prepare, tr("Loading")},
        {VideoCore::LoadCallbackStage::Build, tr("Loading shaders")},
        {VideoCore::LoadCallbackStage::Complete, tr("Starting")},
        {VideoCore::LoadCallbackStage::Warm, tr("Precaching shaders")},
    };
    progressbar_style = {
        {VideoCore::LoadCallbackStage::Prepare, PROGRESSBAR_STYLE_PREPARE},
        {VideoCore::LoadCallbackStage::Build, PROGRESSBAR_STYLE_BUILD},
        {VideoCore::LoadCallbackStage::Complete, PROGRESSBAR_STYLE_COMPLETE},
        {VideoCore::LoadCallbackStage::Warm, PROGRESSBAR_STYLE_BUILD},
    };
}

LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::Prepare(Loader::AppLoader& loader) {
    std::vector<u8> buffer;
    if (loader.ReadBanner(buffer) == Loader::ResultStatus::Success) {
#ifdef YUZU_QT_MOVIE_MISSING
        QPixmap map;
        map.loadFromData(buffer.data(), buffer.size());
        ui->banner->setPixmap(map);
#else
        backing_mem = std::make_unique<QByteArray>(reinterpret_cast<char*>(buffer.data()),
                                                   static_cast<int>(buffer.size()));
        backing_buf = std::make_unique<QBuffer>(backing_mem.get());
        backing_buf->open(QIODevice::ReadOnly);
        animation = std::make_unique<QMovie>(backing_buf.get(), QByteArray());
        animation->start();
        ui->banner->setMovie(animation.get());
#endif
        buffer.clear();
    }
    if (loader.ReadLogo(buffer) == Loader::ResultStatus::Success) {
        QPixmap map;
        map.loadFromData(buffer.data(), static_cast<uint>(buffer.size()));
        ui->logo->setPixmap(map);
    }
    std::string title;
    game_label->setText(loader.ReadTitle(title) == Loader::ResultStatus::Success
                            ? QString::fromStdString(title)
                            : QString{});
    game_label->setVisible(!game_label->text().isEmpty());

    previous_stage = VideoCore::LoadCallbackStage::Complete;
    OnLoadProgress(VideoCore::LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::OnLoadComplete() {
    fadeout_animation->start(QPropertyAnimation::KeepWhenStopped);
}

void LoadingScreen::OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value,
                                   std::size_t total) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    // reset the timer if the stage changes
    if (stage != previous_stage) {
        ui->progress_bar->setStyleSheet(QString::fromUtf8(progressbar_style[stage]));
        // Hide the progress bar during the prepare stage
        if (stage == VideoCore::LoadCallbackStage::Prepare) {
            ui->progress_bar->hide();
        } else {
            ui->progress_bar->show();
        }
        skip_button->setVisible(stage == VideoCore::LoadCallbackStage::Warm);
        skip_button->setEnabled(true);
        previous_stage = stage;
        // A new stage has its own pace.
        stage_start = now;
        samples.clear();
        smoothed_seconds = -1.0;
        estimate_text.clear();
        last_estimate = {};
    }
    // update the max of the progress bar if the number of shaders change
    if (total != previous_total) {
        ui->progress_bar->setMaximum(static_cast<int>(total));
        previous_total = total;
    }
    // Reset the progress bar ranges if compilation is done
    if (stage == VideoCore::LoadCallbackStage::Complete) {
        ui->progress_bar->setRange(0, 0);
    }

    // update labels and progress bar
    ui->stage->setText(stage_translations[stage]);
    if ((stage == VideoCore::LoadCallbackStage::Build ||
         stage == VideoCore::LoadCallbackStage::Warm) &&
        total > 0) {
        const QLocale locale;
        const int percent = static_cast<int>(value * 100 / total);
        QString details = tr("%1 of %2  ·  %3%")
                              .arg(locale.toString(static_cast<qulonglong>(value)),
                                   locale.toString(static_cast<qulonglong>(total)))
                              .arg(percent);
        const QString estimate = EstimateRemaining(value, total);
        if (!estimate.isEmpty()) {
            details += QStringLiteral("  ·  ") + estimate;
        }
        ui->value->setText(details);
    } else {
        ui->value->clear();
    }
    ui->progress_bar->setValue(static_cast<int>(value));
}

QString LoadingScreen::EstimateRemaining(std::size_t value, std::size_t total) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    samples.emplace_back(now, value);
    // The rate over the last 10 seconds.
    while (samples.size() > 2 && now - samples.front().first > seconds{10}) {
        samples.pop_front();
    }
    if (now - last_estimate < seconds{1}) {
        return estimate_text;
    }
    last_estimate = now;
    const auto& [first_time, first_value] = samples.front();
    const double elapsed = duration<double>(now - first_time).count();
    // Nothing reliable in the first seconds of a stage, or while nothing moves.
    if (now - stage_start < seconds{3} || elapsed < 1.0 || value <= first_value ||
        value >= total) {
        if (value >= total) {
            estimate_text.clear();
        }
        return estimate_text;
    }
    const double rate = static_cast<double>(value - first_value) / elapsed;
    const double remaining = static_cast<double>(total - value) / rate;
    smoothed_seconds =
        smoothed_seconds < 0 ? remaining : smoothed_seconds * 0.7 + remaining * 0.3;

    const auto seconds_left = static_cast<long long>(smoothed_seconds);
    if (seconds_left < 60) {
        estimate_text = tr("less than a minute left");
    } else if (seconds_left < 3600) {
        estimate_text = tr("about %n min left", "", static_cast<int>((seconds_left + 59) / 60));
    } else {
        const long long minutes = (seconds_left + 59) / 60;
        estimate_text = tr("about %1 h %2 min left")
                            .arg(minutes / 60)
                            .arg(minutes % 60, 2, 10, QLatin1Char('0'));
    }
    return estimate_text;
}

void LoadingScreen::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

void LoadingScreen::Clear() {
#ifndef YUZU_QT_MOVIE_MISSING
    animation.reset();
    backing_buf.reset();
    backing_mem.reset();
#endif
}
