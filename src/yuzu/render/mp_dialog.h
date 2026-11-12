// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QDialog>
#include <QTimer>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

/// The Ocarina of Time multiplayer window (Tools menu, Ctrl+Shift+M). Outside a room: a name, and
/// create a room or join one by its code. In a room: its code to share, the connection, the
/// players and a way out.
class MultiplayerDialog : public QDialog {
    Q_OBJECT

public:
    /// `started` runs after a room starts (the main window shows the multiplayer overlay).
    explicit MultiplayerDialog(std::function<void()> started, QWidget* parent = nullptr);

private:
    QWidget* CreateLobby();
    QWidget* CreateRoom();
    void Start(const QString& code);
    void Refresh();

    std::function<void()> started;
    QStackedWidget* pages;
    QLineEdit* name;
    QLineEdit* code;
    QPushButton* join;
    QLabel* room_code;
    QPushButton* copy;
    QLabel* status_dot;
    QLabel* status;
    QLabel* players_title;
    QListWidget* players;
    QTimer timer;
};
