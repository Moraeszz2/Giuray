// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTimer>
#include <QWidget>

#include "core/nx2_multiplayer.h"

class QWidget;

/// Draws the other Ocarina of Time player (Core::Nx2Multiplayer) over the render window: a
/// marker at the remote Link's position, projected with the local camera.
class MultiplayerOverlay : public QWidget {
    Q_OBJECT

public:
    explicit MultiplayerOverlay(QWidget* render_window, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void Tick();

    QWidget* render_window;
    QTimer timer;
    Core::Nx2Multiplayer::Frame frame;
};
