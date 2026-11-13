// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game/game_list.h"
#include "game/search_field.h"
#include "yuzu/main_window.h"

#include <QAction>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

#include "yuzu/util/glyphs.h"

// TODO: Remove GameList dependence?
GameListSearchField::KeyReleaseEater::KeyReleaseEater(GameList* gamelist_, QObject* parent)
    : QObject(parent), gamelist{gamelist_} {}

// EventFilter in order to process systemkeys while editing the searchfield
bool GameListSearchField::KeyReleaseEater::eventFilter(QObject* obj, QEvent* event) {
    // If it isn't a KeyRelease event then continue with standard event processing
    if (event->type() != QEvent::KeyRelease)
        return QObject::eventFilter(obj, event);

    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
    QString edit_filter_text = gamelist->search_field->edit_filter->text().toLower();

    // If the searchfield's text hasn't changed special function keys get checked
    // If no function key changes the searchfield's text the filter doesn't need to get reloaded
    if (edit_filter_text == edit_filter_text_old) {
        switch (keyEvent->key()) {
        // Escape: Resets the searchfield
        case Qt::Key_Escape: {
            if (edit_filter_text_old.isEmpty()) {
                return QObject::eventFilter(obj, event);
            } else {
                gamelist->search_field->edit_filter->clear();
                edit_filter_text.clear();
            }
            break;
        }
        // Return and Enter
        // If the enter key gets pressed first checks how many and which entry is visible
        // If there is only one result launch this game
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            if (gamelist->search_field->visible == 1) {
                const QString file_path = gamelist->GetLastFilterResultItem();

                // To avoid loading error dialog loops while confirming them using enter
                // Also users usually want to run a different game after closing one
                gamelist->search_field->edit_filter->clear();
                edit_filter_text.clear();
                emit gamelist->GameChosen(file_path);
            } else {
                return QObject::eventFilter(obj, event);
            }
            break;
        }
        default:
            return QObject::eventFilter(obj, event);
        }
    }
    edit_filter_text_old = edit_filter_text;
    return QObject::eventFilter(obj, event);
}

void GameListSearchField::setFilterResult(int visible_, int total_) {
    visible = visible_;
    total = total_;

    // Only while filtering: the count of all games says nothing.
    label_filter_result->setText(tr("%1 of %n result(s)", "", total).arg(visible));
    label_filter_result->setVisible(edit_filter->isVisible() && !edit_filter->text().isEmpty());
}

QString GameListSearchField::filterText() const {
    return edit_filter->text();
}

void GameListSearchField::clear() {
    edit_filter->clear();
}

void GameListSearchField::setFocus() {
    if (edit_filter->isVisible()) {
        edit_filter->setFocus();
    }
}

void GameListSearchField::SetSearchVisible(bool) {
    // The search field always shows: it is the library's main way to find a game, and a quiet
    // field costs less than a button that has to be clicked first.
    edit_filter->setVisible(true);
    button_filter_close->setVisible(false);
    button_search->setVisible(false);
    label_filter_result->setVisible(!edit_filter->text().isEmpty());
}

void GameListSearchField::SetMenus(QMenu* view_mode, QMenu* icon_size) {
    button_icon_size->setMenu(icon_size);
    button_icon_size->setVisible(icon_size != nullptr);

    // The view modes as a segmented switcher: each button is the menu's action, so the View
    // menu and the switcher stay in step.
    auto* const layout = static_cast<QHBoxLayout*>(view_switch->layout());
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (view_mode == nullptr) {
        view_switch->setVisible(false);
        return;
    }
    for (QAction* action : view_mode->actions()) {
        if (!action->isCheckable()) {
            continue;
        }
        const QString name = action->objectName();
        const auto glyph = name.contains(QStringLiteral("Grid"))       ? Glyphs::Glyph::Grid
                           : name.contains(QStringLiteral("Carousel")) ? Glyphs::Glyph::Carousel
                                                                       : Glyphs::Glyph::List;
        action->setIcon(Glyphs::Icon(glyph));
        auto* const button = new QToolButton(view_switch);
        button->setObjectName(QStringLiteral("ViewSwitchButton"));
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
    }
    view_switch->setVisible(true);
}

GameListSearchField::Tab GameListSearchField::CurrentTab() const {
    const int id = tab_group->checkedId();
    return id < 0 ? Tab::All : static_cast<Tab>(id);
}

void GameListSearchField::SetTabCounts(const std::array<int, 5>& counts) {
    tab_counts = counts;
    UpdateTabTexts();
}

void GameListSearchField::SetTabsVisible(bool visible_) {
    tabs->setVisible(visible_);
}

void GameListSearchField::UpdateTabTexts() {
    const std::array<QString, 5> names{tr("All"), tr("Switch 2"), tr("Switch"), tr("Favorites"),
                                       tr("Recently played")};
    for (size_t i = 0; i < tab_buttons.size(); ++i) {
        // Recently played has no count: it is an order as much as a filter.
        const bool counted = i != static_cast<size_t>(Tab::Recent);
        tab_buttons[i]->setText(counted ? QStringLiteral("%1   %2").arg(names[i]).arg(tab_counts[i])
                                        : names[i]);
    }
}

GameListSearchField::GameListSearchField(GameList* parent) : QWidget{parent} {
    setObjectName(QStringLiteral("LibraryBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* const key_release_eater = new KeyReleaseEater(parent, this);

    edit_filter = new QLineEdit;
    edit_filter->setObjectName(QStringLiteral("LibrarySearch"));
    edit_filter->addAction(Glyphs::Icon(Glyphs::Glyph::Search), QLineEdit::LeadingPosition);
    edit_filter->installEventFilter(key_release_eater);
    edit_filter->setClearButtonEnabled(true);
    edit_filter->setFixedWidth(320);
    connect(edit_filter, &QLineEdit::textChanged, parent, &GameList::OnTextChanged);

    // The shortcut sits inside the field, at its right end.
    label_shortcut = new QLabel(QStringLiteral("Ctrl F"), edit_filter);
    label_shortcut->setObjectName(QStringLiteral("LibrarySearchShortcut"));
    auto* const search_inner = new QHBoxLayout(edit_filter);
    search_inner->setContentsMargins(0, 0, 8, 0);
    search_inner->addStretch();
    search_inner->addWidget(label_shortcut);
    connect(edit_filter, &QLineEdit::textChanged, label_shortcut,
            [this](const QString& text) { label_shortcut->setVisible(text.isEmpty()); });

    label_filter_result = new QLabel;
    label_filter_result->setObjectName(QStringLiteral("LibraryResults"));
    label_filter_result->setVisible(false);

    // Hides the search part (the View menu's filter bar setting).
    button_filter_close = new QToolButton;
    button_filter_close->setObjectName(QStringLiteral("LibraryClose"));
    button_filter_close->setText(QStringLiteral("✕"));
    button_filter_close->setAutoRaise(true);
    button_filter_close->setCursor(Qt::ArrowCursor);
    connect(button_filter_close, &QToolButton::clicked, parent, &GameList::OnFilterCloseClicked);

    button_search = new QToolButton;
    button_search->setIcon(QIcon::fromTheme(QStringLiteral("search")));
    button_search->setAutoRaise(true);
    button_search->setVisible(false);
    connect(button_search, &QToolButton::clicked, this, [parent] {
        parent->main_window->filterBarSetChecked(true);
    });

    auto* const title = new QLabel;
    title->setObjectName(QStringLiteral("LibraryTitle"));
    label_title = title;

    // Platform, favorites and recent tabs: one checked at a time.
    tabs = new QWidget;
    auto* const tabs_layout = new QHBoxLayout(tabs);
    tabs_layout->setContentsMargins(0, 0, 0, 0);
    tabs_layout->setSpacing(4);
    tab_group = new QButtonGroup(this);
    tab_group->setExclusive(true);
    for (size_t i = 0; i < tab_buttons.size(); ++i) {
        auto* const button = new QToolButton;
        button->setObjectName(QStringLiteral("LibraryTab"));
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        tab_group->addButton(button, static_cast<int>(i));
        tabs_layout->addWidget(button);
        tab_buttons[i] = button;
    }
    tab_buttons[0]->setChecked(true);
    connect(tab_group, &QButtonGroup::idClicked, this, &GameListSearchField::TabChanged);

    button_icon_size = new QToolButton;
    button_icon_size->setObjectName(QStringLiteral("LibraryIconButton"));
    button_icon_size->setIcon(Glyphs::Icon(Glyphs::Glyph::Sliders));
    button_icon_size->setPopupMode(QToolButton::InstantPopup);
    button_icon_size->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button_icon_size->setFocusPolicy(Qt::NoFocus);
    button_icon_size->setVisible(false);

    view_switch = new QWidget;
    view_switch->setObjectName(QStringLiteral("ViewSwitch"));
    view_switch->setAttribute(Qt::WA_StyledBackground, true);
    auto* const switch_layout = new QHBoxLayout(view_switch);
    switch_layout->setContentsMargins(2, 2, 2, 2);
    switch_layout->setSpacing(2);
    view_switch->setVisible(false);

    button_add_dir = new QPushButton;
    button_add_dir->setProperty("primary", true);
    button_add_dir->setIcon(Glyphs::Icon(Glyphs::Glyph::Plus));
    button_add_dir->setFocusPolicy(Qt::NoFocus);
    connect(button_add_dir, &QPushButton::clicked, parent, &GameList::AddDirectory);

    layout_filter = new QHBoxLayout;
    layout_filter->setContentsMargins(32, 16, 32, 16);
    layout_filter->setSpacing(8);
    layout_filter->addWidget(title);
    layout_filter->addSpacing(32);
    layout_filter->addWidget(tabs);
    layout_filter->addStretch();
    layout_filter->addWidget(label_filter_result);
    layout_filter->addWidget(button_search);
    layout_filter->addWidget(edit_filter);
    layout_filter->addWidget(button_filter_close);
    layout_filter->addSpacing(8);
    layout_filter->addWidget(button_icon_size);
    layout_filter->addSpacing(8);
    layout_filter->addWidget(view_switch);
    layout_filter->addSpacing(16);
    layout_filter->addWidget(button_add_dir);
    setLayout(layout_filter);
    RetranslateUI();
}

void GameListSearchField::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void GameListSearchField::RetranslateUI() {
    label_title->setText(tr("Library"));
    edit_filter->setPlaceholderText(tr("Search"));
    button_search->setToolTip(tr("Search games"));
    button_filter_close->setToolTip(tr("Hide the search bar"));
    button_icon_size->setToolTip(tr("Icon size"));
    UpdateTabTexts();
    button_add_dir->setText(tr("Add Directory"));
    button_add_dir->setToolTip(tr("Add a folder with games to the library"));
}
