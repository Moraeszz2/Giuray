// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <QWidget>

class GameList;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;

/// The game library's bar: the title, the platform and favorites tabs, the search field, the
/// icon size menu, the view switcher and adding a game directory.
class GameListSearchField : public QWidget {
    Q_OBJECT

public:
    enum class Tab { All, Switch2, Switch, Favorites, Recent };

    explicit GameListSearchField(GameList* parent = nullptr);

    Tab CurrentTab() const;
    /// Game counts per tab, shown next to each tab's name.
    void SetTabCounts(const std::array<int, 5>& counts);
    /// The tabs filter the flat views (grid, carousel); the list has its folders instead.
    void SetTabsVisible(bool visible);

    QString filterText() const;
    void setFilterResult(int visible_, int total_);

    void clear();
    void setFocus();

    /// Shows or hides the search part (the filter bar); the rest of the bar stays.
    void SetSearchVisible(bool visible);

    /// The main window's game list mode and icon size menus, offered from the bar.
    void SetMenus(QMenu* view_mode, QMenu* icon_size);

signals:
    void TabChanged();

private:
    void UpdateTabTexts();

    void changeEvent(QEvent*) override;
    void RetranslateUI();

    class KeyReleaseEater : public QObject {
    public:
        explicit KeyReleaseEater(GameList* gamelist_, QObject* parent = nullptr);

    private:
        GameList* gamelist = nullptr;
        QString edit_filter_text_old;

    protected:
        // EventFilter in order to process systemkeys while editing the searchfield
        bool eventFilter(QObject* obj, QEvent* event) override;
    };
    int visible = 0;
    int total = 0;

    QHBoxLayout* layout_filter = nullptr;
    QLabel* label_title = nullptr;
    QLineEdit* edit_filter = nullptr;
    QLabel* label_filter_result = nullptr;
    QToolButton* button_filter_close = nullptr;
    QToolButton* button_search = nullptr; ///< Brings the hidden search field back.
    QLabel* label_shortcut = nullptr; ///< The search shortcut, inside the field.
    QWidget* tabs = nullptr;
    QButtonGroup* tab_group = nullptr;
    std::array<QToolButton*, 5> tab_buttons{};
    std::array<int, 5> tab_counts{};
    QToolButton* button_icon_size = nullptr;
    QWidget* view_switch = nullptr;
    QPushButton* button_add_dir = nullptr;
};
