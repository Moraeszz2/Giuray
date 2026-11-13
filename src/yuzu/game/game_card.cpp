// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include "game_card.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"

namespace {

/// The cover scaled once to the box's device pixels (smooth), so it stays sharp on any screen
/// scale instead of being stretched while it is drawn.
QPixmap SharpCover(const QPixmap& source, const QSize& box, const QPaintDevice* device) {
    const qreal ratio = device != nullptr ? device->devicePixelRatioF() : 1.0;
    const QSize pixels = box * ratio;
    const QString key = QStringLiteral("giuray-cover-%1-%2x%3")
                            .arg(source.cacheKey())
                            .arg(pixels.width())
                            .arg(pixels.height());
    QPixmap scaled;
    if (!QPixmapCache::find(key, &scaled)) {
        scaled = source.scaled(pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPixmapCache::insert(key, scaled);
    }
    scaled.setDevicePixelRatio(ratio);
    return scaled;
}

} // namespace

GameCard::GameCard(QObject* parent) : QStyledItemDelegate{parent} {
    setObjectName("GameCard");
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    if (!index.isValid())
        return;

    if (m_columns >= 1 && UISettings::values.game_icon_size.GetValue() > 0) {
        paintGridCard(painter, option, index);
        return;
    }

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
    const QColor surface = dark ? QColor(0x1F, 0x1F, 0x23) : palette.base().color();
    const QColor raised = dark ? QColor(0x27, 0x27, 0x2A) : palette.alternateBase().color();
    QColor backgroundColor = selected || hovered ? raised : surface;
    QColor borderColor = dark ? QColor(0x30, 0x30, 0x36) : palette.mid().color();
    if (hovered && !selected) {
        borderColor = dark ? QColor(0x3F, 0x3F, 0x46) : palette.dark().color();
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

        // A game without an icon gets a transparent one: show a quiet tile with its initial
        // under it, as the list does. A real icon is opaque and covers the tile.
        QColor tile = textColor;
        tile.setAlpha(18);
        painter->fillRect(iconRect, tile);
        const QString name = index.data(Qt::DisplayRole).toString();
        const auto letter = std::find_if(name.cbegin(), name.cend(),
                                         [](QChar c) { return c.isLetterOrNumber(); });
        if (letter != name.cend()) {
            QFont initial_font = option.font;
            initial_font.setWeight(QFont::DemiBold);
            initial_font.setPixelSize(std::max(12, iconRect.height() * 2 / 5));
            painter->setFont(initial_font);
            QColor initial = textColor;
            initial.setAlpha(120);
            painter->setPen(initial);
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

// The grid shows each game as its cover, as on a console home screen: no card around it, the
// title and play time under it, the platform and favorite marks on the cover, and a ring around
// the selected one that reads from a distance with a controller.
void GameCard::paintGridCard(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    constexpr int radius = 10;
    const QRect card = getCardRect(option, index);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    const QPalette& palette = option.palette;
    const bool dark = palette.base().color().lightness() < 128;
    const QColor text = palette.text().color();
    const QColor title_color = dark ? QColor(0xFA, 0xFA, 0xFA) : text;
    const QColor muted = dark ? QColor(0x80, 0x80, 0x89) : palette.placeholderText().color();
    const QColor accent = palette.highlight().color();

    const int text_height = UISettings::values.show_game_name.GetValue() ? gridTextHeight : 0;
    const int side = std::max(16, std::min(card.width(), card.height() - text_height));
    const QRect cover{card.left() + (card.width() - side) / 2, card.top(), side, side};

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addRoundedRect(cover, radius, radius);

    QString title_text = index.data(GameListItemPath::TitleRole).toString();
    if (title_text.isEmpty()) {
        title_text = index.data(Qt::DisplayRole).toString().split(QLatin1Char('\n')).first();
    }
    title_text = title_text.trimmed();

    // Cover, or a quiet tile with the initial for a game without an icon
    painter->fillPath(clip, dark ? QColor(0x27, 0x27, 0x2A) : palette.alternateBase().color());
    const auto letter = std::find_if(title_text.cbegin(), title_text.cend(),
                                     [](QChar c) { return c.isLetterOrNumber(); });
    if (letter != title_text.cend()) {
        QFont initial = option.font;
        initial.setWeight(QFont::DemiBold);
        initial.setPixelSize(std::max(14, side * 3 / 10));
        painter->setFont(initial);
        painter->setPen(dark ? QColor(0xA1, 0xA1, 0xAA) : muted);
        painter->drawText(cover, Qt::AlignCenter, QString(*letter).toUpper());
    }
    QPixmap pixmap = index.data(GameListItemPath::CoverRole).value<QPixmap>();
    if (pixmap.isNull()) {
        pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
    }
    if (!pixmap.isNull()) {
        painter->save();
        painter->setClipPath(clip);
        painter->drawPixmap(cover, SharpCover(pixmap, cover.size(), painter->device()));
        painter->restore();
    }

    // Hairline so dark covers keep their edge on the dark window
    painter->setBrush(Qt::NoBrush);
    painter->setPen(
        QPen(hovered && !selected ? QColor(255, 255, 255, 40) : QColor(255, 255, 255, 16), 1));
    painter->drawRoundedRect(QRectF(cover).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    const u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();

    // Favorite mark
    if (program_id != 0 && side >= 96 && UISettings::values.favorited_ids.contains(program_id)) {
        const QPointF c(cover.right() - 19, cover.top() + 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0x18, 0x18, 0x1B, 204));
        painter->drawEllipse(c, 12, 12);
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2) ? 2.6 : 6.0;
            const double a = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
            star << QPointF(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
        }
        painter->setBrush(QColor(0xFA, 0xFA, 0xFA));
        painter->drawPolygon(star);
    }

    // Selection ring, with a gap so it reads on any cover
    if (selected) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(dark ? QColor(0xFA, 0xFA, 0xFA) : accent, 2));
        painter->drawRoundedRect(QRectF(cover).adjusted(-4, -4, 4, 4), radius + 4, radius + 4);
    }

    // Title and play time
    if (text_height > 0) {
        QFont title_font = option.font;
        title_font.setPixelSize(13);
        title_font.setWeight(QFont::Medium);
        QFont detail_font = option.font;
        detail_font.setPixelSize(12);
        const QFontMetrics title_metrics{title_font};
        const QFontMetrics detail_metrics{detail_font};

        const int left = cover.left() + 2;
        const int width = cover.width() - 4;
        int top = cover.bottom() + 1 + 10;

        painter->setFont(title_font);
        painter->setPen(selected ? title_color : text);
        painter->drawText(QRect(left, top, width, title_metrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          title_metrics.elidedText(title_text, Qt::ElideRight, width));
        top += title_metrics.height() + 2;

        const QString play_time = index.sibling(index.row(), GameListModel::COLUMN_PLAY_TIME)
                                      .data(Qt::DisplayRole)
                                      .toString();
        const QString detail = play_time.isEmpty() ? tr("Never played") : play_time;
        painter->setFont(detail_font);
        painter->setPen(muted);
        painter->drawText(QRect(left, top, width, detail_metrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          detail_metrics.elidedText(detail, Qt::ElideRight, width));
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
