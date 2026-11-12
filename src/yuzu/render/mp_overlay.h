// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTimer>
#include <QWidget>

#include "core/nx2_multiplayer/multiplayer.h"

class QWidget;

/// Draws the other Ocarina of Time players' names (Core::Nx2Multiplayer) over the render window,
/// above their Links, projected with the local camera.
class MultiplayerOverlay : public QWidget {
    Q_OBJECT

public:
    explicit MultiplayerOverlay(QWidget* render_window, QWidget* parent = nullptr);
    ~MultiplayerOverlay() override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void Tick();

    QWidget* render_window;
    QTimer timer;
    Core::Nx2Multiplayer::Frame frame;
};
