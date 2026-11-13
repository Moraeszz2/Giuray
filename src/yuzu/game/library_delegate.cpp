// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QAbstractItemView>
#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QHelpEvent>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QToolTip>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/game/library_delegate.h"

namespace {

constexpr int RowHeight = 48;
constexpr int Margin = 8;
constexpr int IconSize = 24;
constexpr int TextGap = 16;

bool IsDirectoryRow(GameListItemType type) {
    switch (type) {
    case GameListItemType::CustomDir:
    case GameListItemType::SdmcDir:
    case GameListItemType::UserNandDir:
    case GameListItemType::SysNandDir:
    case GameListItemType::AddDir:
    case GameListItemType::Favorites:
        return true;
    default:
        return false;
    }
}

QColor Mix(const QColor& color, int alpha) {
    QColor mixed = color;
    mixed.setAlpha(alpha);
    return mixed;
}

/// Whether a game's icon is the empty stand-in for a game without one (fully transparent).
bool IsBlank(const QPixmap& pixmap) {
    static QHash<qint64, bool> blank;
    const auto it = blank.constFind(pixmap.cacheKey());
    if (it != blank.cend()) {
        return *it;
    }
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    bool empty = true;
    for (int y = 0; y < image.height() && empty; y += 2) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += 2) {
            if (qAlpha(line[x]) != 0) {
                empty = false;
                break;
            }
        }
    }
    blank.insert(pixmap.cacheKey(), empty);
    return empty;
}

int GameIconSize() {
    return std::clamp(static_cast<int>(UISettings::values.game_icon_size.GetValue()), 0, 64);
}

/// The row's background (hover, selection, separator) as the style draws it, without content.
void DrawPanel(QPainter* painter, QStyleOptionViewItem panel) {
    panel.text.clear();
    panel.icon = QIcon{};
    panel.features &= ~QStyleOptionViewItem::HasDecoration;
    const QWidget* widget = panel.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &panel, painter, widget);
}

/// A game row's first column: its icon (or a quiet tile with its initial), the title, and the
/// second line of the row's text (title ID, file name...) in a muted tone.
void PaintGame(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index,
               const QString& text) {
    const QRect rect = option.rect;
    const int icon_size = GameIconSize();
    int text_left = rect.left() + Margin;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const QStringList lines = text.split(QLatin1Char('\n'));
    const QString title = lines.value(0).trimmed();
    const QString detail =
        lines.size() > 1 ? lines.mid(1).join(QStringLiteral("  ")).trimmed() : QString{};
    if (icon_size > 0) {
        const QRect icon_rect{text_left, rect.center().y() - icon_size / 2 + 1, icon_size,
                              icon_size};
        QPainterPath clip;
        clip.addRoundedRect(icon_rect, 6, 6);
        const QPixmap pixmap = index.data(Qt::DecorationRole).value<QPixmap>();
        if (!pixmap.isNull() && !IsBlank(pixmap)) {
            painter->setClipPath(clip);
            painter->drawPixmap(icon_rect, pixmap);
            painter->setClipping(false);
        } else {
            painter->fillPath(clip, Mix(option.palette.color(QPalette::Text), 18));
            QFont initial = option.font;
            initial.setPixelSize(std::max(12, icon_size * 2 / 5));
            initial.setWeight(QFont::DemiBold);
            painter->setFont(initial);
            painter->setPen(Mix(option.palette.color(QPalette::Text), 120));
            painter->drawText(icon_rect, Qt::AlignCenter, title.left(1).toUpper());
        }
        text_left = icon_rect.right() + 1 + TextGap;
    }

    const int text_width = rect.right() - Margin - text_left;
    QFont title_font = option.font;
    title_font.setWeight(QFont::Medium);
    QFont detail_font = option.font;
    detail_font.setPixelSize(12);
    const QFontMetrics title_metrics{title_font};
    const QFontMetrics detail_metrics{detail_font};
    const QColor text_color = option.palette.color(QPalette::Text);
    const int block =
        title_metrics.height() + (detail.isEmpty() ? 0 : 2 + detail_metrics.height());
    const int top = rect.top() + (rect.height() - block) / 2;
    painter->setFont(title_font);
    painter->setPen(text_color);
    painter->drawText(QRect{text_left, top, text_width, title_metrics.height()},
                      Qt::AlignVCenter | Qt::AlignLeft,
                      title_metrics.elidedText(title, Qt::ElideRight, text_width));
    if (!detail.isEmpty()) {
        painter->setFont(detail_font);
        painter->setPen(Mix(text_color, 140));
        painter->drawText(
            QRect{text_left, top + title_metrics.height() + 2, text_width, detail_metrics.height()},
            Qt::AlignVCenter | Qt::AlignLeft,
            detail_metrics.elidedText(detail, Qt::ElideRight, text_width));
    }
    painter->restore();
}

/// The add-ons column in one line: the update's version and how many other add-ons there are.
QString AddOnsSummary(const QString& text) {
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    static const QRegularExpression update{QStringLiteral(R"(^Update \(([^)]+)\))")};
    QString version;
    int others = 0;
    for (const QString& line : lines) {
        const auto match = update.match(line.trimmed());
        if (match.hasMatch()) {
            version = match.captured(1);
        } else {
            ++others;
        }
    }
    QStringList parts;
    if (!version.isEmpty()) {
        parts << LibraryDelegate::tr("Update %1").arg(version);
    }
    if (others > 0) {
        parts << (others == 1 ? LibraryDelegate::tr("1 add-on")
                              : LibraryDelegate::tr("%1 add-ons").arg(others));
    }
    return parts.join(QStringLiteral("  ·  "));
}

} // Anonymous namespace

bool LibraryDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view,
                                const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::ToolTip && index.column() == GameListModel::COLUMN_ADD_ONS &&
        index.sibling(index.row(), 0).data(GameListItem::TypeRole).value<GameListItemType>() ==
            GameListItemType::Game) {
        const QString full = index.data(Qt::DisplayRole).toString();
        if (!full.isEmpty()) {
            QToolTip::showText(event->globalPos(), full, view);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

void LibraryDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                            const QModelIndex& index) const {
    const auto type = index.data(GameListItem::TypeRole).value<GameListItemType>();
    QStyleOptionViewItem panel = option;
    initStyleOption(&panel, index);
    const QIcon icon = panel.icon;
    const QString display = panel.text;

    const auto row_type =
        index.sibling(index.row(), 0).data(GameListItem::TypeRole).value<GameListItemType>();
    if (row_type == GameListItemType::Game) {
        if (index.column() == 0) {
            DrawPanel(painter, panel);
            PaintGame(painter, option, index, display);
            return;
        }
        // The other columns (file type, size, play time, add-ons): smaller and muted.
        QStyleOptionViewItem detail = option;
        detail.font.setPixelSize(12);
        detail.palette.setColor(QPalette::Text, Mix(option.palette.color(QPalette::Text), 165));
        detail.displayAlignment = Qt::AlignVCenter | Qt::AlignLeft;
        if (index.column() == GameListModel::COLUMN_ADD_ONS) {
            // One line, so every game row keeps its height; the full list is the tooltip.
            QStyleOptionViewItem summary = detail;
            initStyleOption(&summary, index);
            // From the model: the style option's text has its line breaks turned into
            // QChar::LineSeparator.
            summary.text = AddOnsSummary(index.data(Qt::DisplayRole).toString());
            summary.textElideMode = Qt::ElideRight;
            const QWidget* widget = summary.widget;
            QStyle* style = widget ? widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &summary, painter, widget);
            return;
        }
        QStyledItemDelegate::paint(painter, detail, index);
        return;
    }
    if (index.column() != 0 || !IsDirectoryRow(type)) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    DrawPanel(painter, panel);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const QRect rect = option.rect;
    const QRect icon_rect{rect.left() + Margin, rect.center().y() - IconSize / 2 + 1, IconSize,
                          IconSize};
    if (!icon.isNull()) {
        icon.paint(painter, icon_rect);
    }

    const bool dark = UISettings::IsDarkTheme();
    const QColor text_color = option.palette.color(QPalette::Text);
    const QColor muted = Mix(text_color, 140);
    const QColor accent = dark ? QColor{0xEF, 0x44, 0x44} : option.palette.color(QPalette::Highlight);
    const QColor danger = dark ? QColor{0xF5, 0x9E, 0x0B} : QColor{0xB4, 0x53, 0x09};

    QString title = display;
    QString detail;
    bool missing = false;
    const int games = index.model()->rowCount(index);
    switch (type) {
    case GameListItemType::CustomDir: {
        const QFileInfo info{display};
        missing = !info.exists();
        if (!info.fileName().isEmpty()) {
            title = info.fileName();
        }
        detail = display;
        const int dir_index = index.data(GameListDir::GameDirRole).toInt();
        if (dir_index >= 0 && dir_index < UISettings::values.game_dirs.size() &&
            UISettings::values.game_dirs[dir_index].deep_scan) {
            detail += QStringLiteral("  ·  ") + tr("includes subfolders");
        }
        if (!missing) {
            detail += QStringLiteral("  ·  ") + tr("%n game(s)", "", games);
        }
        break;
    }
    case GameListItemType::AddDir:
        break;
    default:
        detail = tr("%n game(s)", "", games);
        break;
    }

    const int text_left = icon_rect.right() + 1 + TextGap;
    const int text_width = rect.right() - Margin - text_left;
    QFont title_font = option.font;
    title_font.setWeight(QFont::Medium);
    QFont detail_font = option.font;
    detail_font.setPixelSize(12);
    const QFontMetrics title_metrics{title_font};
    const QFontMetrics detail_metrics{detail_font};

    if (detail.isEmpty() && !missing) {
        painter->setFont(title_font);
        painter->setPen(type == GameListItemType::AddDir ? accent : text_color);
        painter->drawText(QRect{text_left, rect.top(), text_width, rect.height()},
                          Qt::AlignVCenter | Qt::AlignLeft,
                          title_metrics.elidedText(title, Qt::ElideRight, text_width));
        painter->restore();
        return;
    }

    const int block = title_metrics.height() + 2 + detail_metrics.height();
    const int top = rect.top() + (rect.height() - block) / 2;
    painter->setFont(title_font);
    painter->setPen(text_color);
    painter->drawText(QRect{text_left, top, text_width, title_metrics.height()},
                      Qt::AlignVCenter | Qt::AlignLeft,
                      title_metrics.elidedText(title, Qt::ElideRight, text_width));

    painter->setFont(detail_font);
    QRect detail_rect{text_left, top + title_metrics.height() + 2, text_width,
                      detail_metrics.height()};
    if (missing) {
        // A small marker, not an alarm: the word in red, the path as usual.
        const QString label = tr("Not found") + QStringLiteral("  ·  ");
        painter->setPen(danger);
        painter->drawText(detail_rect, Qt::AlignVCenter | Qt::AlignLeft, label);
        detail_rect.setLeft(detail_rect.left() + detail_metrics.horizontalAdvance(label));
    }
    painter->setPen(muted);
    painter->drawText(detail_rect, Qt::AlignVCenter | Qt::AlignLeft,
                      detail_metrics.elidedText(detail, Qt::ElideMiddle, detail_rect.width()));
    painter->restore();
}

QSize LibraryDelegate::sizeHint(const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const auto type = index.data(GameListItem::TypeRole).value<GameListItemType>();
    const auto row_type =
        index.sibling(index.row(), 0).data(GameListItem::TypeRole).value<GameListItemType>();
    if (IsDirectoryRow(type)) {
        size.setHeight(RowHeight);
    } else if (row_type == GameListItemType::Game) {
        // The icon with 8 px above and below, never less than a directory row; every column
        // of the row (the add-ons list included) takes that height.
        size.setHeight(std::max(RowHeight, GameIconSize() + 2 * Margin));
    }
    return size;
}
