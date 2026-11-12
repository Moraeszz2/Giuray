// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include "game_card.h"
#include "qt_common/config/uisettings.h"

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    constexpr int cardCornerRadius = 14;

    const QRect cardRect = getCardRect(option, index);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // The selected card keeps its surface and gets an accent ring; in the carousel the others
    // recede so the selection reads at a glance, as on a console home screen.
    // Style sheets only set the view's base, text and highlight colors, so a dark theme gets the
    // Giuray surfaces here and a light one derives them from its palette.
    const QPalette& palette = option.palette;
    const bool dark = palette.base().color().lightness() < 128;
    const QColor accent = palette.highlight().color();
    const QColor textColor = palette.text().color();
    const QColor surface = dark ? QColor(0x15, 0x19, 0x25) : palette.base().color();
    const QColor raised = dark ? QColor(0x1C, 0x21, 0x31) : palette.alternateBase().color();
    QColor backgroundColor = selected || hovered ? raised : surface;
    QColor borderColor = dark ? QColor(0x26, 0x2B, 0x3B) : palette.mid().color();
    if (hovered && !selected) {
        borderColor = dark ? QColor(0x35, 0x3B, 0x50) : palette.dark().color();
    }
    if (m_columns < 1 && !selected) {
        painter->setOpacity(hovered ? 0.9 : 0.62);
    }

    painter->setBrush(backgroundColor);
    painter->setPen(QPen(borderColor, 1));
    painter->drawRoundedRect(cardRect, cardCornerRadius, cardCornerRadius);
    if (selected) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 3));
        painter->drawRoundedRect(QRectF(cardRect).adjusted(-2.5, -2.5, 2.5, 2.5),
                                 cardCornerRadius + 2.5, cardCornerRadius + 2.5);
    }

    const u32 icon_size = UISettings::values.game_icon_size.GetValue();
    QPixmap icon_pixmap = index.data(Qt::DecorationRole).value<QPixmap>();

    QRect iconRect;
    if (!icon_pixmap.isNull()) {
        QSize scaled = icon_pixmap.size();
        scaled.scale(icon_size, icon_size, Qt::KeepAspectRatio);

        iconRect = {cardRect.left() + (cardRect.width() - scaled.width()) / 2,
                    cardRect.top() + cardMargin - 1, scaled.width(), scaled.height()};

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter->save();
        QPainterPath clip_path;
        clip_path.addRoundedRect(iconRect, cardCornerRadius - 4, cardCornerRadius - 4);
        painter->setClipPath(clip_path);

        // A game without an icon gets a transparent one: show a tile with its initial under it,
        // in the logo's gradient. A real icon is opaque and covers the tile.
        QLinearGradient tile(iconRect.topLeft(), iconRect.bottomRight());
        tile.setColorAt(0, QColor(0x5B, 0x3F, 0xD9));
        tile.setColorAt(1, QColor(0x1E, 0x8F, 0xD8));
        painter->fillRect(iconRect, tile);
        const QString name = index.data(Qt::DisplayRole).toString();
        const auto letter = std::find_if(name.cbegin(), name.cend(),
                                         [](QChar c) { return c.isLetterOrNumber(); });
        if (letter != name.cend()) {
            QFont initial_font = option.font;
            initial_font.setBold(true);
            initial_font.setPixelSize(std::max(12, iconRect.height() / 2));
            painter->setFont(initial_font);
            painter->setPen(QColor(255, 255, 255, 235));
            painter->drawText(iconRect, Qt::AlignCenter, QString(*letter).toUpper());
        }

        painter->drawPixmap(iconRect, icon_pixmap);
        painter->restore();
    } else {
        iconRect = {cardRect.left() + cardMargin, cardRect.top() + cardMargin,
                    static_cast<int>(icon_size), static_cast<int>(icon_size)};
    }

    if (UISettings::values.show_game_name.GetValue()) {
        QRect textRect = cardRect;
        textRect.setTop(iconRect.bottom() + cardMargin);
        textRect.adjust(cardMargin, 0, -cardMargin, -cardMargin);

        QString title = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();

        painter->setPen(textColor);
        QFont font = option.font;
        font.setBold(true);
        font.setPixelSize(std::max(11.0, std::sqrt(static_cast<double>(icon_size))));
        painter->setFont(font);

        painter->drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, title);
    }

    painter->restore();
}

QRect GameCard::getCardRect(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int cell_width = option.rect.width();

    const int card_width = cell_width - m_padding;

    int card_left, card_top, card_height;

    if (m_columns >= 1) {
        // grid mode

        // center everything in-line, such that the leftmost and rightmost cards
        // have ~ equal padding to the edge of the viewport
        // spacing between each card is larger, but equal to each other
        const int column = index.row() % m_columns;

        const int row_width = m_columns * cell_width;
        const int total_gap = row_width - cardMargin * 2 - m_columns * card_width;
        const int gap = (m_columns > 1) ? (total_gap / (m_columns - 1)) : 0;
        card_left =
            option.rect.left() - column * cell_width + cardMargin + column * (card_width + gap) + 4;

        // fill cell vertically
        card_top = option.rect.top() + cardMargin;
        card_height = option.rect.height() - cardMargin - 1;
    } else {
        // carousel mode
        card_left = option.rect.left() + cardMargin + 4;

        // the delegate itself takes up the full height, but the card itself
        // gets centered
        const int content_height = m_contentSize.height() - cardMargin;
        const int cell_height = option.rect.height();

        card_height = std::min(content_height, cell_height - cardMargin * 2) - 1;
        card_top = option.rect.top() + (cell_height - card_height) / 2;
    }

    return QRect(card_left, card_top, card_width - cardMargin, card_height);
}

bool GameCard::hitTest(const QPoint& point, const QModelIndex& index, const QWidget* widget,
                       const QRect& cellRect) const {
    QStyleOptionViewItem option;
    option.initFrom(widget);
    option.rect = cellRect;
    return getCardRect(option, index).contains(point);
}

QSize GameCard::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return m_size;
}

void GameCard::setSize(const QSize& newSize, const QSize& contentSize, const int padding,
                       const int columns) {
    m_size = newSize;
    m_contentSize = contentSize;
    m_padding = padding;
    m_columns = columns;
}
