// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFrame>
#include <QList>

class QAction;
class QToolButton;
class QVBoxLayout;

/// The console-style navigation rail at the left of the library. It shows while the library (or its
/// empty placeholder) does, and hides with them when a game takes over the window.
class NavSidebar : public QFrame {
    Q_OBJECT

public:
    enum class Glyph { Library, Open, Install, Multiplayer, Settings };

    explicit NavSidebar(QWidget* parent = nullptr);

    /// Adds a button that triggers `action`. `bottom` puts it under the stretch.
    QToolButton* AddAction(QAction* action, Glyph glyph, const QString& label, bool bottom = false);
    /// The rail follows these widgets' visibility: shown while any of them is.
    void Follow(const QList<QWidget*>& widgets);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void UpdateVisibility();

    QVBoxLayout* layout = nullptr;
    QList<QWidget*> followed;
    int top_count = 0;
};
