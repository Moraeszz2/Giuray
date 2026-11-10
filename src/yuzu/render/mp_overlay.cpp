// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdlib>

#include <QPainter>
#include <QPainterPath>

#include "qt_common/qt_common.h"
#include "yuzu/render/mp_overlay.h"

namespace {

struct Vec3 {
    float x, y, z;
};

Vec3 Sub(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

float Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 Normalize(Vec3 v) {
    const float length = std::sqrt(Dot(v, v));
    return length > 0 ? Vec3{v.x / length, v.y / length, v.z / length} : v;
}

/// Vertical field of view, measured against Link's feet on screen in Castle Town (about 50°).
float TanHalfFov() {
    static const float value = [] {
        const char* text = std::getenv("EDEN_NX2_MP_FOV");
        const float degrees = text ? static_cast<float>(std::atof(text)) : 50.0f;
        return std::tan(degrees * 3.14159265f / 360.0f);
    }();
    return value;
}

} // Anonymous namespace

MultiplayerOverlay::MultiplayerOverlay(QWidget* render_window_, QWidget* parent)
    : QWidget(parent), render_window{render_window_} {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput |
                   Qt::WindowDoesNotAcceptFocus);
    connect(&timer, &QTimer::timeout, this, &MultiplayerOverlay::Tick);
    timer.start(33);
}

MultiplayerOverlay::~MultiplayerOverlay() {
    Core::Nx2Multiplayer::Shutdown();
}

void MultiplayerOverlay::Tick() {
    if (QtCommon::system) {
        frame = Core::Nx2Multiplayer::Tick(*QtCommon::system);
    }
    if (frame.remote.valid) {
        const auto& target = frame.remote.position;
        float jump = 0;
        for (int i = 0; i < 3; ++i) {
            jump += (target[i] - shown[i]) * (target[i] - shown[i]);
        }
        // Snap on the first packet and after a teleport (scene change, warp).
        if (!shown_valid || jump > 25.0f * 25.0f) {
            shown = target;
        } else {
            for (int i = 0; i < 3; ++i) {
                shown[i] += (target[i] - shown[i]) * 0.35f;
            }
        }
        shown_valid = true;
    } else {
        shown_valid = false;
    }
    if (render_window && render_window->isVisible()) {
        const QRect area{render_window->mapToGlobal(QPoint{0, 0}), render_window->size()};
        if (geometry() != area) {
            setGeometry(area);
        }
        if (!isVisible()) {
            show();
        }
        raise();
    }
    update();
}

void MultiplayerOverlay::paintEvent(QPaintEvent*) {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor{255, 255, 255, 200});
    painter.drawText(QPoint{8, height() - 8}, QString::fromStdString(frame.status));
    if (!frame.valid || !frame.remote.valid) {
        return;
    }
    // The game draws 16:9 into the window, letterboxed.
    const float aspect = 16.0f / 9.0f;
    float view_w = static_cast<float>(width());
    float view_h = view_w / aspect;
    if (view_h > height()) {
        view_h = static_cast<float>(height());
        view_w = view_h * aspect;
    }
    const float left = (width() - view_w) / 2;
    const float top = (height() - view_h) / 2;

    const Vec3 forward = Normalize(Sub(frame.at, frame.eye));
    const Vec3 right = Normalize(Cross(forward, Vec3{0, 1, 0}));
    const Vec3 up = Cross(right, forward);
    const auto project = [&](const std::array<float, 3>& point, QPointF& out) {
        const Vec3 d = Sub(point, frame.eye);
        const float z = Dot(d, forward);
        if (z < 0.1f) {
            return false;
        }
        const float ndc_x = Dot(d, right) / z / (TanHalfFov() * aspect);
        const float ndc_y = Dot(d, up) / z / TanHalfFov();
        out = {left + (ndc_x + 1) / 2 * view_w, top + (1 - ndc_y) / 2 * view_h};
        return true;
    };
    const auto& feet = shown;
    const std::array<float, 3> head{feet[0], feet[1] + 1.4f, feet[2]};
    QPointF feet_point;
    QPointF head_point;
    if (!project(feet, feet_point) || !project(head, head_point)) {
        return;
    }
    const float height_px = static_cast<float>(feet_point.y() - head_point.y());
    const float radius = std::max(6.0f, height_px * 0.25f);
    painter.setPen(QPen{QColor{40, 255, 120, 230}, 3});
    painter.setBrush(QColor{40, 255, 120, 70});
    painter.drawEllipse(QPointF{feet_point.x(), feet_point.y() - height_px / 2}, radius,
                        std::max(radius, height_px / 2));
    // Facing: an arrow on the ground, one unit ahead of the feet.
    const auto& facing = frame.remote.facing;
    const float facing_length = std::sqrt(facing[0] * facing[0] + facing[1] * facing[1]);
    QPointF ahead_point;
    if (facing_length > 0.01f &&
        project({feet[0] + facing[0] / facing_length, feet[1], feet[2] + facing[1] / facing_length},
                ahead_point)) {
        painter.setPen(QPen{QColor{255, 230, 60, 240}, 3});
        painter.drawLine(feet_point, ahead_point);
        const QPointF direction = ahead_point - feet_point;
        const float length = std::hypot(direction.x(), direction.y());
        if (length > 4) {
            const QPointF unit = direction / length;
            const QPointF side{-unit.y(), unit.x()};
            const float tip = std::min(12.0f, length * 0.4f);
            painter.drawLine(ahead_point, ahead_point - unit * tip + side * tip * 0.6);
            painter.drawLine(ahead_point, ahead_point - unit * tip - side * tip * 0.6);
        }
    }
    const QString name = frame.remote.name.empty() ? QStringLiteral("P2")
                                                   : QString::fromStdString(frame.remote.name);
    painter.setPen(QColor{40, 255, 120, 255});
    painter.drawText(head_point + QPointF{-10, -6}, name);
}
