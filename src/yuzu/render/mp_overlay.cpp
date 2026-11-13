// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <cstdlib>

#include <QPainter>
#include <QFontMetricsF>
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
        const char* text = std::getenv("GIURAY_NX2_MP_FOV");
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

    // Tags use the theme's window color, translucent, with the accent for the marker dot.
    const QColor tag_background{0x18, 0x18, 0x1B, 210};
    const QColor tag_text{0xFA, 0xFA, 0xFA};
    const QColor accent{0xEF, 0x44, 0x44};

    const QString status = QString::fromStdString(frame.status);
    if (!status.isEmpty()) {
        const QFontMetricsF status_metrics{painter.font()};
        const QRectF pill{8, height() - 8 - status_metrics.height() - 8,
                          status_metrics.horizontalAdvance(status) + 20,
                          status_metrics.height() + 8};
        painter.setPen(Qt::NoPen);
        painter.setBrush(tag_background);
        painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        painter.setPen(tag_text);
        painter.drawText(pill, Qt::AlignCenter, status);
    }
    if (!frame.valid || frame.remotes.empty()) {
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
    // Each other player's name over their Link's head.
    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() * 1.3);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetricsF metrics{font};
    for (const auto& remote : frame.remotes) {
        QPointF head;
        if (!remote.body ||
            !project({(*remote.body)[0], (*remote.body)[1] + 1.7f, (*remote.body)[2]}, head)) {
            continue;
        }
        const QString name = QString::fromStdString(remote.name);
        constexpr qreal dot = 8;
        const qreal text_width = metrics.horizontalAdvance(name);
        const QRectF pill{head.x() - (text_width + dot + 30) / 2, head.y() - metrics.height() - 10,
                          text_width + dot + 30, metrics.height() + 10};
        painter.setPen(Qt::NoPen);
        painter.setBrush(tag_background);
        painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF{pill.left() + 12 + dot / 2, pill.center().y()}, dot / 2,
                            dot / 2);
        painter.setPen(tag_text);
        painter.drawText(pill.adjusted(12 + dot + 6, 0, -12, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         name);
    }
}
