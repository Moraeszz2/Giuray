// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

/// Draws the game list's directory rows (game folders, installed titles, favorites and the "add
/// a directory" row) as one line each of the same height: the icon, the name, and below it the
/// path and how many games it holds, with a small marker for a folder that is missing. Game rows
/// are drawn as before.
class LibraryDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
