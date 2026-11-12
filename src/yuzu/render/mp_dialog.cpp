// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/nx2_multiplayer/multiplayer.h"
#include "yuzu/render/mp_dialog.h"

namespace {

constexpr int CodeLength = 6;
constexpr int MaxPlayers = 4;

QLabel* Hint(const QString& text) {
    auto* label = new QLabel{text};
    label->setWordWrap(true);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}

} // Anonymous namespace

MultiplayerDialog::MultiplayerDialog(std::function<void()> started_, QWidget* parent)
    : QDialog{parent}, started{std::move(started_)} {
    setWindowTitle(QStringLiteral("Multiplayer - Ocarina of Time"));
    setMinimumWidth(400);

    pages = new QStackedWidget;
    pages->addWidget(CreateLobby());
    pages->addWidget(CreateRoom());
    auto* layout = new QVBoxLayout{this};
    layout->addWidget(pages);

    connect(&timer, &QTimer::timeout, this, &MultiplayerDialog::Refresh);
    timer.start(500);
    Refresh();
}

QWidget* MultiplayerDialog::CreateLobby() {
    auto* page = new QWidget;

    name = new QLineEdit{QSettings{}.value(QStringLiteral("nx2_multiplayer/name")).toString()};
    name->setPlaceholderText(QStringLiteral("Como os outros vão te ver"));
    name->setMaxLength(15);
    auto* name_row = new QHBoxLayout;
    name_row->addWidget(new QLabel{QStringLiteral("Seu nome:")});
    name_row->addWidget(name, 1);

    auto* host_box = new QGroupBox{QStringLiteral("Criar uma sala")};
    auto* host = new QPushButton{QStringLiteral("Criar sala")};
    host->setProperty("primary", true);
    auto* host_layout = new QVBoxLayout{host_box};
    host_layout->addWidget(Hint(QStringLiteral(
        "Você recebe um código para mandar aos amigos. Até 4 jogadores por sala.")));
    host_layout->addWidget(host);

    auto* join_box = new QGroupBox{QStringLiteral("Entrar na sala de um amigo")};
    code = new QLineEdit;
    code->setPlaceholderText(QStringLiteral("CÓDIGO"));
    code->setMaxLength(CodeLength);
    code->setAlignment(Qt::AlignCenter);
    code->setValidator(new QRegularExpressionValidator{
        QRegularExpression{QStringLiteral("[A-Za-z0-9]{0,6}")}, code});
    QFont code_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    code_font.setPointSizeF(code_font.pointSizeF() * 1.4);
    code_font.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    code->setFont(code_font);
    join = new QPushButton{QStringLiteral("Entrar")};
    join->setProperty("primary", true);
    join->setEnabled(false);
    auto* join_row = new QHBoxLayout;
    join_row->addWidget(code, 1);
    join_row->addWidget(join);
    auto* join_layout = new QVBoxLayout{join_box};
    join_layout->addWidget(Hint(QStringLiteral("Digite o código que o amigo te mandou.")));
    join_layout->addLayout(join_row);

    auto* layout = new QVBoxLayout{page};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(name_row);
    layout->addWidget(host_box);
    layout->addWidget(join_box);
    layout->addWidget(Hint(QStringLiteral(
        "Todos precisam estar na mesma versão do Giuray e com o jogo aberto.")));

    connect(host, &QPushButton::clicked, this, [this] { Start({}); });
    connect(code, &QLineEdit::textEdited, this, [this](const QString& text) {
        const int cursor = code->cursorPosition();
        code->setText(text.toUpper());
        code->setCursorPosition(cursor);
        join->setEnabled(text.size() == CodeLength);
    });
    const auto join_room = [this] {
        if (code->text().size() == CodeLength) {
            Start(code->text());
        }
    };
    connect(join, &QPushButton::clicked, this, join_room);
    connect(code, &QLineEdit::returnPressed, this, join_room);
    return page;
}

QWidget* MultiplayerDialog::CreateRoom() {
    auto* page = new QWidget;

    room_code = new QLabel;
    QFont big = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    big.setPointSizeF(big.pointSizeF() * 2.6);
    big.setBold(true);
    big.setLetterSpacing(QFont::AbsoluteSpacing, 6);
    room_code->setFont(big);
    room_code->setAlignment(Qt::AlignCenter);
    room_code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    copy = new QPushButton{QStringLiteral("Copiar código")};

    status_dot = new QLabel{QStringLiteral("●")};
    status = new QLabel;
    status->setWordWrap(true);
    auto* status_row = new QHBoxLayout;
    status_row->addWidget(status_dot);
    status_row->addWidget(status, 1);

    players_title = new QLabel;
    QFont bold = players_title->font();
    bold.setBold(true);
    players_title->setFont(bold);
    players = new QListWidget;
    players->setSelectionMode(QAbstractItemView::NoSelection);
    players->setFocusPolicy(Qt::NoFocus);
    players->setMaximumHeight(players->fontMetrics().height() * 6);

    auto* leave = new QPushButton{QStringLiteral("Sair da sala")};

    auto* layout = new QVBoxLayout{page};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(Hint(QStringLiteral("Código da sala: mande para os amigos")));
    layout->addWidget(room_code);
    layout->addWidget(copy);
    layout->addSpacing(6);
    layout->addLayout(status_row);
    layout->addSpacing(6);
    layout->addWidget(players_title);
    layout->addWidget(players);
    layout->addWidget(leave);

    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(room_code->text());
        copy->setText(QStringLiteral("Copiado!"));
        QTimer::singleShot(1500, copy, [this] { copy->setText(QStringLiteral("Copiar código")); });
    });
    connect(leave, &QPushButton::clicked, this, [this] {
        Core::Nx2Multiplayer::LeaveRoom();
        pages->setCurrentIndex(0);
    });
    return page;
}

void MultiplayerDialog::Start(const QString& room) {
    QSettings{}.setValue(QStringLiteral("nx2_multiplayer/name"), name->text());
    Core::Nx2Multiplayer::StartRoom(room.toStdString(), name->text().toStdString());
    if (started) {
        started();
    }
    pages->setCurrentIndex(1);
    Refresh();
}

void MultiplayerDialog::Refresh() {
    namespace Mp = Core::Nx2Multiplayer;
    if (!Mp::InRoom()) {
        pages->setCurrentIndex(0);
        return;
    }
    pages->setCurrentIndex(1);
    const QString text = QString::fromStdString(Mp::RoomCode());
    room_code->setText(text.isEmpty() ? QStringLiteral("······") : text);
    copy->setEnabled(!text.isEmpty());

    const QString line = QString::fromStdString(Mp::NetworkStatus());
    const qsizetype colon = line.indexOf(QStringLiteral(": "));
    status->setText(colon >= 0 ? line.mid(colon + 2) : line);
    const bool error = line.contains(QStringLiteral("erro"));
    const QString color = error              ? QStringLiteral("#e5534b")
                          : Mp::Connected() ? QStringLiteral("#3fb950")
                                            : QStringLiteral("#d29922");
    status_dot->setStyleSheet(QStringLiteral("color: %1;").arg(color));

    const auto names = Mp::PlayerNames();
    players_title->setText(QStringLiteral("Jogadores (%1/%2)")
                               .arg(static_cast<int>(names.size()) + 1)
                               .arg(MaxPlayers));
    players->clear();
    const QString own = name->text().trimmed();
    players->addItem(QStringLiteral("%1 (você)").arg(own.isEmpty() ? QStringLiteral("P1") : own));
    for (const auto& other : names) {
        players->addItem(QString::fromStdString(other));
    }
}
