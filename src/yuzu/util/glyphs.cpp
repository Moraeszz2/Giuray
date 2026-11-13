// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <numbers>

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include "yuzu/util/glyphs.h"

namespace Glyphs {

namespace {

// Theme colors (dist/qt_themes/giuray): muted text at rest, near-white when active.
const QColor RestColor{0xA1, 0xA1, 0xAA};
const QColor ActiveColor{0xFA, 0xFA, 0xFA};

QPolygonF StarShape(const QPointF& center, qreal outer, qreal inner) {
    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const qreal radius = (i % 2) ? inner : outer;
        const qreal angle = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
        star << QPointF(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    }
    return star;
}

// Drawn on a 16x16 grid.
void Draw(QPainter& p, Glyph glyph, const QColor& color) {
    p.setPen(QPen{color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
    p.setBrush(Qt::NoBrush);
    switch (glyph) {
    case Glyph::Search:
        p.drawEllipse(QPointF{7, 7}, 4.5, 4.5);
        p.drawLine(QPointF{10.4, 10.4}, QPointF{14, 14});
        break;
    case Glyph::Sliders:
        p.drawLine(QPointF{2, 4.5}, QPointF{14, 4.5});
        p.drawLine(QPointF{2, 8}, QPointF{14, 8});
        p.drawLine(QPointF{2, 11.5}, QPointF{14, 11.5});
        p.setBrush(color);
        p.drawEllipse(QPointF{10, 4.5}, 1.6, 1.6);
        p.drawEllipse(QPointF{5, 8}, 1.6, 1.6);
        p.drawEllipse(QPointF{9, 11.5}, 1.6, 1.6);
        break;
    case Glyph::List:
        for (const qreal y : {4.0, 8.0, 12.0}) {
            p.drawLine(QPointF{2.5, y}, QPointF{3, y});
            p.drawLine(QPointF{5.5, y}, QPointF{13.5, y});
        }
        break;
    case Glyph::Grid:
        for (const QPointF origin : {QPointF{2, 2}, QPointF{9, 2}, QPointF{2, 9}, QPointF{9, 9}}) {
            p.drawRoundedRect(QRectF{origin, QSizeF{5, 5}}, 1.2, 1.2);
        }
        break;
    case Glyph::Carousel:
        p.drawRoundedRect(QRectF{4.5, 3, 7, 10}, 1.4, 1.4);
        p.drawLine(QPointF{2, 5}, QPointF{2, 11});
        p.drawLine(QPointF{14, 5}, QPointF{14, 11});
        break;
    case Glyph::Play: {
        QPainterPath path;
        path.moveTo(4.5, 2.5);
        path.lineTo(13, 8);
        path.lineTo(4.5, 13.5);
        path.closeSubpath();
        p.setBrush(color);
        p.drawPath(path);
        break;
    }
    case Glyph::Star:
        p.drawPolygon(StarShape({8, 8.4}, 6.2, 2.7));
        break;
    case Glyph::StarFilled:
        p.setBrush(color);
        p.drawPolygon(StarShape({8, 8.4}, 6.2, 2.7));
        break;
    case Glyph::More:
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        for (const qreal x : {3.5, 8.0, 12.5}) {
            p.drawEllipse(QPointF{x, 8}, 1.3, 1.3);
        }
        break;
    case Glyph::Plus:
        p.drawLine(QPointF{8, 3}, QPointF{8, 13});
        p.drawLine(QPointF{3, 8}, QPointF{13, 8});
        break;
    case Glyph::Volume: {
        QPainterPath speaker;
        speaker.moveTo(2.5, 6);
        speaker.lineTo(5, 6);
        speaker.lineTo(8.5, 3);
        speaker.lineTo(8.5, 13);
        speaker.lineTo(5, 10);
        speaker.lineTo(2.5, 10);
        speaker.closeSubpath();
        p.drawPath(speaker);
        p.drawArc(QRectF{7.5, 5, 5, 6}, -60 * 16, 120 * 16);
        break;
    }
    }
}

} // namespace

QPixmap Pixmap(Glyph glyph, const QColor& color, int size) {
    constexpr qreal scale = 2.0;
    QPixmap pixmap(QSize{size, size} * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 16.0, size / 16.0);
    Draw(p, glyph, color);
    return pixmap;
}

QIcon Icon(Glyph glyph) {
    QIcon icon;
    icon.addPixmap(Pixmap(glyph, RestColor), QIcon::Normal, QIcon::Off);
    icon.addPixmap(Pixmap(glyph, ActiveColor), QIcon::Active, QIcon::Off);
    icon.addPixmap(Pixmap(glyph, ActiveColor), QIcon::Selected, QIcon::Off);
    icon.addPixmap(Pixmap(glyph, ActiveColor), QIcon::Normal, QIcon::On);
    icon.addPixmap(Pixmap(glyph, ActiveColor), QIcon::Active, QIcon::On);
    return icon;
}

} // namespace Glyphs
