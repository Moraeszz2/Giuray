// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game/game_list.h"
#include "game/search_field.h"
#include "yuzu/main_window.h"

#include <QAction>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

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

void GameListSearchField::SetSearchVisible(bool visible_) {
    edit_filter->setVisible(visible_);
    button_filter_close->setVisible(visible_);
    button_search->setVisible(!visible_);
    label_filter_result->setVisible(visible_ && !edit_filter->text().isEmpty());
}

void GameListSearchField::SetMenus(QMenu* view_mode, QMenu* icon_size) {
    button_view->setMenu(view_mode);
    button_icon_size->setMenu(icon_size);
    button_view->setVisible(view_mode != nullptr);
    button_icon_size->setVisible(icon_size != nullptr);
}

GameListSearchField::GameListSearchField(GameList* parent) : QWidget{parent} {
    setObjectName(QStringLiteral("LibraryBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* const key_release_eater = new KeyReleaseEater(parent, this);

    edit_filter = new QLineEdit;
    edit_filter->setObjectName(QStringLiteral("LibrarySearch"));
    edit_filter->addAction(QIcon::fromTheme(QStringLiteral("search")),
                           QLineEdit::LeadingPosition);
    edit_filter->installEventFilter(key_release_eater);
    edit_filter->setClearButtonEnabled(true);
    edit_filter->setMaximumWidth(360);
    connect(edit_filter, &QLineEdit::textChanged, parent, &GameList::OnTextChanged);

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

    const auto menu_button = [this] {
        auto* button = new QToolButton;
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(true);
        button->setVisible(false);
        return button;
    };
    button_view = menu_button();
    button_view->setIcon(QIcon::fromTheme(QStringLiteral("view-list")));
    button_icon_size = menu_button();
    button_icon_size->setIcon(QIcon::fromTheme(QStringLiteral("view-grid")));

    button_add_dir = new QPushButton;
    button_add_dir->setProperty("primary", true);
    button_add_dir->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    button_add_dir->setFocusPolicy(Qt::NoFocus);
    connect(button_add_dir, &QPushButton::clicked, parent, &GameList::AddDirectory);

    layout_filter = new QHBoxLayout;
    layout_filter->setContentsMargins(16, 8, 16, 8);
    layout_filter->setSpacing(8);
    layout_filter->addWidget(title);
    layout_filter->addSpacing(8);
    layout_filter->addWidget(button_search);
    layout_filter->addWidget(edit_filter, 1);
    layout_filter->addWidget(label_filter_result);
    layout_filter->addWidget(button_filter_close);
    layout_filter->addStretch();
    layout_filter->addWidget(button_view);
    layout_filter->addWidget(button_icon_size);
    layout_filter->addSpacing(8);
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
    edit_filter->setPlaceholderText(tr("Search games"));
    button_search->setToolTip(tr("Search games"));
    button_filter_close->setToolTip(tr("Hide the search bar"));
    button_view->setText(tr("View"));
    button_view->setToolTip(tr("Game list mode"));
    button_icon_size->setText(tr("Icon Size"));
    button_add_dir->setText(tr("Add Directory"));
    button_add_dir->setToolTip(tr("Add a folder with games to the library"));
}
