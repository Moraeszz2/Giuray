// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <numbers>

#include <QAction>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include "yuzu/util/nav_sidebar.h"

namespace {

constexpr int GlyphSize = 22;
constexpr qreal Scale = 2.0;

// Theme colors: muted text for the resting icon, near-white when hovered or pressed.
const QColor RestColor{0xA3, 0xA9, 0xBE};
const QColor ActiveColor{0xF8, 0xF9, 0xFC};

void DrawGlyph(QPainter& p, NavSidebar::Glyph glyph) {
    // Drawn on a 22x22 grid.
    switch (glyph) {
    case NavSidebar::Glyph::Library:
        for (const QPointF origin : {QPointF{3, 3}, QPointF{12, 3}, QPointF{3, 12}, QPointF{12, 12}}) {
            p.drawRoundedRect(QRectF{origin, QSizeF{7, 7}}, 2, 2);
        }
        break;
    case NavSidebar::Glyph::Open: {
        QPainterPath path;
        path.moveTo(3, 6);
        path.lineTo(9, 6);
        path.lineTo(11, 8);
        path.lineTo(19, 8);
        path.lineTo(19, 18);
        path.lineTo(3, 18);
        path.closeSubpath();
        p.drawPath(path);
        QPainterPath play;
        play.moveTo(9.5, 10.5);
        play.lineTo(14, 13);
        play.lineTo(9.5, 15.5);
        play.closeSubpath();
        p.drawPath(play);
        break;
    }
    case NavSidebar::Glyph::Install:
        p.drawLine(QPointF{11, 3}, QPointF{11, 13});
        p.drawPolyline(QPolygonF{{QPointF{7, 9.5}, QPointF{11, 13.5}, QPointF{15, 9.5}}});
        p.drawPolyline(QPolygonF{{QPointF{3, 13}, QPointF{3, 19}, QPointF{19, 19}, QPointF{19, 13}}});
        break;
    case NavSidebar::Glyph::Multiplayer:
        p.drawEllipse(QPointF{8, 7.5}, 3, 3);
        p.drawArc(QRectF{2.5, 12.5, 11, 10}, 0, 180 * 16);
        p.drawEllipse(QPointF{15.5, 8.5}, 2.5, 2.5);
        p.drawArc(QRectF{12.5, 13.5, 8, 8}, 0, 120 * 16);
        break;
    case NavSidebar::Glyph::Settings: {
        p.drawEllipse(QPointF{11, 11}, 3, 3);
        p.drawEllipse(QPointF{11, 11}, 6.5, 6.5);
        for (int i = 0; i < 8; ++i) {
            const qreal angle = i * std::numbers::pi / 4;
            const QPointF dir{std::cos(angle), std::sin(angle)};
            p.drawLine(QPointF{11, 11} + dir * 6.5, QPointF{11, 11} + dir * 9);
        }
        break;
    }
    }
}

QPixmap GlyphPixmap(NavSidebar::Glyph glyph, const QColor& color) {
    QPixmap pixmap(QSize{GlyphSize, GlyphSize} * Scale);
    pixmap.setDevicePixelRatio(Scale);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen{color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
    p.setBrush(Qt::NoBrush);
    DrawGlyph(p, glyph);
    return pixmap;
}

QIcon GlyphIcon(NavSidebar::Glyph glyph) {
    QIcon icon;
    icon.addPixmap(GlyphPixmap(glyph, RestColor), QIcon::Normal);
    icon.addPixmap(GlyphPixmap(glyph, ActiveColor), QIcon::Active);
    icon.addPixmap(GlyphPixmap(glyph, ActiveColor), QIcon::Selected);
    icon.addPixmap(GlyphPixmap(glyph, ActiveColor), QIcon::Normal, QIcon::On);
    return icon;
}

} // namespace

NavSidebar::NavSidebar(QWidget* parent) : QFrame{parent} {
    setObjectName(QStringLiteral("NavSidebar"));
    setFixedWidth(84);

    layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 16, 8, 16);
    layout->setSpacing(6);

    auto* logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("NavLogo"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(QIcon::fromTheme(QStringLiteral("giuray")).pixmap(40, 40));
    layout->addWidget(logo);
    layout->addSpacing(16);
    layout->addStretch(1);
}

QToolButton* NavSidebar::AddAction(QAction* action, Glyph glyph, const QString& label,
                                   bool bottom) {
    auto* button = new QToolButton(this);
    button->setObjectName(QStringLiteral("NavButton"));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIcon(GlyphIcon(glyph));
    button->setIconSize(QSize{GlyphSize, GlyphSize});
    button->setText(label);
    button->setToolTip(action->text().remove(QLatin1Char('&')));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setEnabled(action->isEnabled());

    connect(button, &QToolButton::clicked, action, &QAction::trigger);
    connect(action, &QAction::changed, button,
            [button, action] { button->setEnabled(action->isEnabled()); });

    // Top buttons go between the logo (and its spacing) and the stretch, bottom ones after it.
    if (bottom) {
        layout->addWidget(button);
    } else {
        layout->insertWidget(2 + top_count++, button);
    }
    return button;
}

void NavSidebar::Follow(const QList<QWidget*>& widgets) {
    followed = widgets;
    for (QWidget* widget : followed) {
        widget->installEventFilter(this);
    }
    UpdateVisibility();
}

bool NavSidebar::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) {
        UpdateVisibility();
    }
    return QFrame::eventFilter(object, event);
}

void NavSidebar::UpdateVisibility() {
    bool any = false;
    for (const QWidget* widget : followed) {
        any = any || !widget->isHidden();
    }
    setVisible(any);
}
