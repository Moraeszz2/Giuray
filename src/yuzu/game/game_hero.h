// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QModelIndex>

class QLabel;
class QPushButton;

/// The panel under the carousel: the selected game's name, details and its actions.
class GameHero : public QFrame {
    Q_OBJECT

public:
    explicit GameHero(QWidget* parent = nullptr);

    /// Shows the game at `index` (any column of its row); an invalid index shows the empty state.
    void SetIndex(const QModelIndex& index);

signals:
    void PlayRequested();
    void PropertiesRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    void RetranslateUI();

    QPersistentModelIndex current;
    QLabel* title = nullptr;
    QLabel* details = nullptr;
    QPushButton* play = nullptr;
    QPushButton* properties = nullptr;
};
