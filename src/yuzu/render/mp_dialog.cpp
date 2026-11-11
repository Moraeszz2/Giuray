// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "core/nx2_multiplayer.h"
#include "yuzu/render/mp_dialog.h"

MultiplayerDialog::MultiplayerDialog(std::function<void()> started_, QWidget* parent)
    : QDialog{parent}, started{std::move(started_)} {
    setWindowTitle(QStringLiteral("Multiplayer - Ocarina of Time"));
    setMinimumWidth(380);

    QSettings settings;
    name = new QLineEdit{settings.value(QStringLiteral("nx2_multiplayer/name")).toString()};
    name->setPlaceholderText(QStringLiteral("Seu nome"));
    name->setMaxLength(15);

    auto* host = new QPushButton{QStringLiteral("Hospedar")};
    code = new QLineEdit;
    code->setPlaceholderText(QStringLiteral("Codigo da sala do amigo"));
    code->setMaxLength(6);
    auto* join = new QPushButton{QStringLiteral("Entrar")};

    room_code = new QLabel;
    QFont big = room_code->font();
    big.setPointSize(big.pointSize() * 2);
    big.setBold(true);
    room_code->setFont(big);
    room_code->setTextInteractionFlags(Qt::TextSelectableByMouse);
    copy = new QPushButton{QStringLiteral("Copiar codigo")};
    copy->setEnabled(false);
    status = new QLabel;
    status->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Nome:"), name);
    auto* join_row = new QHBoxLayout;
    join_row->addWidget(code);
    join_row->addWidget(join);
    auto* code_row = new QHBoxLayout;
    code_row->addWidget(room_code, 1);
    code_row->addWidget(copy);
    auto* layout = new QVBoxLayout{this};
    layout->addLayout(form);
    layout->addWidget(host);
    layout->addLayout(join_row);
    layout->addLayout(code_row);
    layout->addWidget(status);

    connect(host, &QPushButton::clicked, this, [this] { Start({}); });
    connect(join, &QPushButton::clicked, this, [this] {
        if (!code->text().trimmed().isEmpty()) {
            Start(code->text().trimmed().toUpper());
        }
    });
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(room_code->text()); });
    connect(&timer, &QTimer::timeout, this, &MultiplayerDialog::Refresh);
    timer.start(500);
    Refresh();
}

void MultiplayerDialog::Start(const QString& room) {
    QSettings{}.setValue(QStringLiteral("nx2_multiplayer/name"), name->text());
    Core::Nx2Multiplayer::StartRoom(room.toStdString(), name->text().toStdString());
    if (started) {
        started();
    }
    Refresh();
}

void MultiplayerDialog::Refresh() {
    const QString text = QString::fromStdString(Core::Nx2Multiplayer::RoomCode());
    room_code->setText(text);
    copy->setEnabled(!text.isEmpty());
    const std::string line = Core::Nx2Multiplayer::NetworkStatus();
    status->setText(line.empty() ? QStringLiteral("Abra o jogo, depois hospede ou entre numa sala.")
                                 : QString::fromStdString(line));
}
