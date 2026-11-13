// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QModelIndex>
#include <QPixmap>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;

/// The selected game's cover, drawn to fill its box (cropped, never stretched).
class GameHeroCover : public QWidget {
    Q_OBJECT

public:
    using QWidget::QWidget;

    void SetCover(const QPixmap& pixmap, const QString& title);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap cover;
    QString initial;
};

/// The panel at the top of the grid and the carousel: the game you played last (or the one
/// selected) with its cover, platform, when it was last played, play time, format and size, its
/// add-ons, and Play, Properties, favorite and more actions.
class GameHero : public QFrame {
    Q_OBJECT

public:
    explicit GameHero(QWidget* parent = nullptr);

    /// Shows the game at `index` (any column of its row); an invalid index shows the empty
    /// state. `most_recent` says it is the game played last.
    void SetIndex(const QModelIndex& index, bool most_recent);
    QModelIndex Index() const;

signals:
    void PlayRequested();
    void PropertiesRequested();
    void FavoriteToggled();
    void MoreRequested(const QPoint& global_position);

protected:
    void changeEvent(QEvent* event) override;

private:
    void RetranslateUI();
    void SetChips(const QStringList& add_ons);

    QPersistentModelIndex current;
    bool current_most_recent = false;
    GameHeroCover* cover = nullptr;
    QLabel* eyebrow = nullptr;
    QLabel* title = nullptr;
    QLabel* badge = nullptr;
    QLabel* details = nullptr;
    QWidget* chips = nullptr;
    QHBoxLayout* chips_layout = nullptr;
    QPushButton* play = nullptr;
    QPushButton* properties = nullptr;
    QToolButton* favorite = nullptr;
    QToolButton* more = nullptr;
};
