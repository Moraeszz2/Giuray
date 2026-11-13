// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>

#include <QDate>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QStringList>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/launch_timestamp_cache.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/game_hero.h"
#include "yuzu/util/glyphs.h"

namespace {

constexpr int CoverSize = 288;
constexpr int MaxChips = 4;

QString PlayTimeText(qulonglong seconds) {
    const qulonglong hours = seconds / 3600;
    const qulonglong minutes = (seconds % 3600) / 60;
    if (hours == 0) {
        return GameHero::tr("%1m played").arg(minutes);
    }
    return GameHero::tr("%1h %2m played").arg(hours).arg(minutes);
}

QString LastPlayedText(s64 timestamp) {
    if (timestamp <= 0) {
        return GameHero::tr("Never played");
    }
    const QDate played = QDateTime::fromSecsSinceEpoch(timestamp).date();
    const qint64 days = played.daysTo(QDate::currentDate());
    if (days <= 0) {
        return GameHero::tr("Last played today");
    }
    if (days == 1) {
        return GameHero::tr("Last played yesterday");
    }
    if (days < 30) {
        return GameHero::tr("Last played %1 days ago").arg(days);
    }
    return GameHero::tr("Last played %1").arg(QLocale().toString(played, QLocale::ShortFormat));
}

} // namespace

void GameHeroCover::SetCover(const QPixmap& pixmap, const QString& title_text) {
    cover = pixmap;
    const auto letter = std::find_if(title_text.cbegin(), title_text.cend(),
                                     [](QChar c) { return c.isLetterOrNumber(); });
    initial = letter != title_text.cend() ? QString(*letter).toUpper() : QString{};
    update();
}

void GameHeroCover::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 12, 12);
    painter.setClipPath(clip);
    painter.fillRect(rect(), QColor(0x27, 0x27, 0x2A));
    if (!cover.isNull()) {
        // Fill the box and crop the overflow, centered, like a cover image on a store page.
        // Scaled once to the box's device pixels, so the cover stays sharp.
        const qreal ratio = devicePixelRatioF();
        QPixmap scaled = cover.scaled(size() * ratio, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(ratio);
        const QSizeF logical = scaled.deviceIndependentSize();
        painter.drawPixmap(QPointF{(width() - logical.width()) / 2, (height() - logical.height()) / 2},
                           scaled);
    }
    if (cover.isNull() || cover.toImage().pixelColor(cover.width() / 2, cover.height() / 2).alpha() == 0) {
        QFont font = this->font();
        font.setPixelSize(height() * 2 / 5);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(0xA1, 0xA1, 0xAA));
        painter.drawText(rect(), Qt::AlignCenter, initial);
    }
}

GameHero::GameHero(QWidget* parent) : QFrame{parent} {
    setObjectName(QStringLiteral("GameHero"));
    setAttribute(Qt::WA_StyledBackground, true);

    cover = new GameHeroCover(this);
    cover->setFixedSize(CoverSize, CoverSize);

    eyebrow = new QLabel(this);
    eyebrow->setObjectName(QStringLiteral("GameHeroEyebrow"));
    title = new QLabel(this);
    title->setObjectName(QStringLiteral("GameHeroTitle"));
    title->setTextFormat(Qt::PlainText);

    badge = new QLabel(this);
    badge->setObjectName(QStringLiteral("GameHeroBadge"));
    details = new QLabel(this);
    details->setObjectName(QStringLiteral("GameHeroDetails"));
    details->setTextFormat(Qt::PlainText);
    auto* const meta = new QHBoxLayout;
    meta->setSpacing(12);
    meta->addWidget(badge);
    meta->addWidget(details, 1);

    chips = new QWidget(this);
    chips_layout = new QHBoxLayout(chips);
    chips_layout->setContentsMargins(0, 0, 0, 0);
    chips_layout->setSpacing(8);

    play = new QPushButton(this);
    play->setObjectName(QStringLiteral("GameHeroPlay"));
    play->setProperty("primary", true);
    play->setIcon(Glyphs::Pixmap(Glyphs::Glyph::Play, Qt::white));
    play->setCursor(Qt::PointingHandCursor);
    properties = new QPushButton(this);
    properties->setObjectName(QStringLiteral("GameHeroProperties"));
    properties->setIcon(Glyphs::Icon(Glyphs::Glyph::Sliders));
    properties->setCursor(Qt::PointingHandCursor);
    favorite = new QToolButton(this);
    favorite->setObjectName(QStringLiteral("GameHeroRound"));
    favorite->setCursor(Qt::PointingHandCursor);
    more = new QToolButton(this);
    more->setObjectName(QStringLiteral("GameHeroRound"));
    more->setIcon(Glyphs::Icon(Glyphs::Glyph::More));
    more->setCursor(Qt::PointingHandCursor);
    auto* const actions = new QHBoxLayout;
    actions->setSpacing(12);
    actions->addWidget(play);
    actions->addWidget(properties);
    actions->addWidget(favorite);
    actions->addWidget(more);
    actions->addStretch();

    auto* const text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(0);
    text->addStretch();
    text->addWidget(eyebrow);
    text->addSpacing(8);
    text->addWidget(title);
    text->addSpacing(12);
    text->addLayout(meta);
    text->addSpacing(16);
    text->addWidget(chips);
    text->addSpacing(20);
    text->addLayout(actions);
    text->addStretch();

    auto* const layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 32, 0);
    layout->setSpacing(40);
    layout->addWidget(cover);
    layout->addLayout(text, 1);

    connect(play, &QPushButton::clicked, this, &GameHero::PlayRequested);
    connect(properties, &QPushButton::clicked, this, &GameHero::PropertiesRequested);
    connect(favorite, &QToolButton::clicked, this, &GameHero::FavoriteToggled);
    connect(more, &QToolButton::clicked, this,
            [this] { emit MoreRequested(more->mapToGlobal(QPoint{0, more->height() + 4})); });

    RetranslateUI();
    SetIndex({}, false);
}

QModelIndex GameHero::Index() const {
    return current;
}

void GameHero::SetChips(const QStringList& add_ons) {
    while (QLayoutItem* item = chips_layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // "Max Resolution (built-in) (IPS)" -> the name, and the detail in parentheses.
    static const QRegularExpression parts{QStringLiteral(R"(^(.*?)(?:\s*\((.*)\))?$)")};
    int shown = 0;
    for (const QString& line : add_ons) {
        if (shown == MaxChips) {
            break;
        }
        const auto match = parts.match(line.trimmed());
        const QString name = match.captured(1).trimmed();
        const bool replaced = line.contains(QStringLiteral("replaced"));
        auto* const chip = new QLabel(chips);
        chip->setObjectName(QStringLiteral("GameHeroChip"));
        chip->setProperty("replaced", replaced);
        chip->setTextFormat(Qt::RichText);
        chip->setText(replaced ? tr("%1 · replaced").arg(name.toHtmlEscaped())
                               : QStringLiteral("<span style=\"color:#EF4444\">●</span>&nbsp;&nbsp;%1")
                                     .arg(name.toHtmlEscaped()));
        chip->setToolTip(line);
        chips_layout->addWidget(chip);
        ++shown;
    }
    if (add_ons.size() > shown) {
        auto* const rest = new QLabel(tr("+%1 more").arg(add_ons.size() - shown), chips);
        rest->setObjectName(QStringLiteral("GameHeroChipMuted"));
        rest->setToolTip(add_ons.mid(shown).join(QLatin1Char('\n')));
        chips_layout->addWidget(rest);
    }
    chips_layout->addStretch();
    chips->setVisible(!add_ons.isEmpty());
}

void GameHero::SetIndex(const QModelIndex& index, bool most_recent) {
    const QModelIndex game = index.isValid() ? index.sibling(index.row(), 0) : QModelIndex{};
    const bool is_game =
        game.isValid() &&
        game.data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::Game;
    current = is_game ? QPersistentModelIndex{game} : QPersistentModelIndex{};
    current_most_recent = is_game && most_recent;

    for (QWidget* widget : std::initializer_list<QWidget*>{play, properties, favorite, more}) {
        widget->setEnabled(is_game);
    }
    if (!is_game) {
        eyebrow->setText(tr("LIBRARY"));
        title->setText(tr("Choose a game"));
        badge->setVisible(false);
        details->setText(tr("Browse with the arrows, the mouse or a controller."));
        SetChips({});
        cover->SetCover({}, {});
        favorite->setIcon(Glyphs::Icon(Glyphs::Glyph::Star));
        return;
    }

    QString name = game.data(GameListItemPath::TitleRole).toString();
    if (name.isEmpty()) {
        name = game.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    }
    eyebrow->setText(current_most_recent ? tr("CONTINUE PLAYING") : tr("SELECTED"));
    title->setText(name);
    QPixmap art = game.data(GameListItemPath::CoverRole).value<QPixmap>();
    if (art.isNull()) {
        art = game.data(Qt::DecorationRole).value<QPixmap>();
    }
    cover->SetCover(art, name);

    const u64 program_id = game.data(GameListItemPath::ProgramIdRole).toULongLong();
    const bool switch2 = (program_id >> 56) == 0x04;
    badge->setText(switch2 ? tr("SWITCH 2") : tr("SWITCH"));
    badge->setProperty("switch2", switch2);
    badge->style()->unpolish(badge);
    badge->style()->polish(badge);
    badge->setVisible(false);

    // Add-ons: the update is a detail of the game, the rest are chips.
    const QStringList add_ons_lines =
        game.sibling(game.row(), GameListModel::COLUMN_ADD_ONS)
            .data(Qt::DisplayRole)
            .toString()
            .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression update{QStringLiteral(R"(^Update \(([^)]+)\))")};
    QString update_version;
    QStringList chip_lines;
    for (const QString& line : add_ons_lines) {
        const auto match = update.match(line.trimmed());
        if (match.hasMatch()) {
            update_version = match.captured(1);
        } else {
            chip_lines << line.trimmed();
        }
    }

    QStringList parts;
    parts << LastPlayedText(Core::LaunchTimestampCache::GetLaunchTimestamp(program_id));
    const qulonglong seconds = game.sibling(game.row(), GameListModel::COLUMN_PLAY_TIME)
                                   .data(GameListItemPlayTime::PlayTimeRole)
                                   .toULongLong();
    if (seconds > 0) {
        parts << PlayTimeText(seconds);
    }
    const QString file_type = game.data(GameListItemPath::FileTypeRole).toString();
    const QString size =
        game.sibling(game.row(), GameListModel::COLUMN_SIZE).data(Qt::DisplayRole).toString();
    if (!file_type.isEmpty() || !size.isEmpty()) {
        parts << (file_type.isEmpty() ? size
                  : size.isEmpty()    ? file_type
                                      : QStringLiteral("%1 · %2").arg(file_type, size));
    }
    if (!update_version.isEmpty()) {
        parts << tr("Update %1").arg(update_version);
    }
    details->setText(parts.join(QStringLiteral("     •     ")));
    SetChips(chip_lines);

    const bool favorited = UISettings::values.favorited_ids.contains(program_id);
    favorite->setIcon(favorited ? Glyphs::Pixmap(Glyphs::Glyph::StarFilled, QColor(0xFA, 0xFA, 0xFA))
                                : Glyphs::Icon(Glyphs::Glyph::Star));
    favorite->setToolTip(favorited ? tr("Remove from favorites") : tr("Add to favorites"));
}

void GameHero::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
        SetIndex(current, current_most_recent);
    }
    QFrame::changeEvent(event);
}

void GameHero::RetranslateUI() {
    play->setText(tr("Play"));
    properties->setText(tr("Properties"));
    more->setToolTip(tr("More actions"));
}
