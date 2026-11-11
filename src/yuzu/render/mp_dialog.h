// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QDialog>
#include <QTimer>

class QLabel;
class QLineEdit;
class QPushButton;

/// The Ocarina of Time multiplayer prototype's window (Tools menu, Ctrl+M): host a room and show
/// its code, or join one by its code; shows the connection status while it is open.
class MultiplayerDialog : public QDialog {
    Q_OBJECT

public:
    /// `started` runs after a room starts (the main window shows the multiplayer overlay).
    explicit MultiplayerDialog(std::function<void()> started, QWidget* parent = nullptr);

private:
    void Start(const QString& code);
    void Refresh();

    std::function<void()> started;
    QLineEdit* name;
    QLineEdit* code;
    QLabel* room_code;
    QPushButton* copy;
    QLabel* status;
    QTimer timer;
};
