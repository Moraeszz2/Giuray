// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/game_hero.h"

GameHero::GameHero(QWidget* parent) : QFrame{parent} {
    setObjectName(QStringLiteral("GameHero"));

    auto* text = new QVBoxLayout;
    text->setSpacing(4);
    title = new QLabel(this);
    title->setObjectName(QStringLiteral("GameHeroTitle"));
    title->setTextFormat(Qt::PlainText);
    details = new QLabel(this);
    details->setObjectName(QStringLiteral("GameHeroDetails"));
    details->setTextFormat(Qt::PlainText);
    text->addWidget(title);
    text->addWidget(details);

    play = new QPushButton(this);
    play->setObjectName(QStringLiteral("GameHeroPlay"));
    play->setProperty("primary", true);
    play->setCursor(Qt::PointingHandCursor);
    properties = new QPushButton(this);
    properties->setObjectName(QStringLiteral("GameHeroProperties"));
    properties->setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(32, 16, 32, 24);
    layout->setSpacing(12);
    layout->addLayout(text, 1);
    layout->addWidget(properties);
    layout->addWidget(play);

    connect(play, &QPushButton::clicked, this, &GameHero::PlayRequested);
    connect(properties, &QPushButton::clicked, this, &GameHero::PropertiesRequested);

    RetranslateUI();
    SetIndex({});
}

void GameHero::SetIndex(const QModelIndex& index) {
    const QModelIndex game = index.isValid() ? index.sibling(index.row(), 0) : QModelIndex{};
    const bool is_game =
        game.isValid() &&
        game.data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::Game;
    current = is_game ? QPersistentModelIndex{game} : QPersistentModelIndex{};

    play->setEnabled(is_game);
    properties->setEnabled(is_game);
    if (!is_game) {
        title->setText(tr("Choose a game"));
        details->setText(tr("Use the arrows, the mouse or a controller to browse your library."));
        return;
    }

    QString name = game.data(GameListItemPath::TitleRole).toString();
    if (name.isEmpty()) {
        name = game.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    }
    title->setText(name);

    QStringList parts;
    const QString file_type = game.data(GameListItemPath::FileTypeRole).toString();
    if (!file_type.isEmpty()) {
        parts << file_type;
    }
    const QString play_time =
        game.sibling(game.row(), GameListModel::COLUMN_PLAY_TIME).data(Qt::DisplayRole).toString();
    if (!play_time.isEmpty()) {
        parts << tr("%1 played").arg(play_time);
    }
    const QString size =
        game.sibling(game.row(), GameListModel::COLUMN_SIZE).data(Qt::DisplayRole).toString();
    if (!size.isEmpty()) {
        parts << size;
    }
    details->setText(parts.join(QStringLiteral("  ·  ")));
}

void GameHero::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
        SetIndex(current);
    }
    QFrame::changeEvent(event);
}

void GameHero::RetranslateUI() {
    play->setText(tr("Play"));
    properties->setText(tr("Properties"));
}
