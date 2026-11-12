// SPDX-License-Identifier: GPL-2.0-or-later
// Giuray — tema "giuray": delegate da lista de jogos/diretórios.
#pragma once

#include <QColor>
#include <QStyledItemDelegate>

namespace Giuray::Theme {
// Paleta fixa (espelha giuray.qss)
inline const QColor Window{0x18, 0x18, 0x1B};
inline const QColor Surface{0x1F, 0x1F, 0x23};
inline const QColor Elevated{0x27, 0x27, 0x2A};
inline const QColor Border{0x30, 0x30, 0x36};
inline const QColor BorderStrong{0x3F, 0x3F, 0x46};
inline const QColor Text{0xE4, 0xE4, 0xE7};
inline const QColor Title{0xFA, 0xFA, 0xFA};
inline const QColor Secondary{0xA1, 0xA1, 0xAA};
inline const QColor Muted{0x80, 0x80, 0x89};
inline const QColor Disabled{0x52, 0x52, 0x5B};
inline const QColor Accent{0xDC, 0x26, 0x26};
inline const QColor AccentHover{0xEF, 0x44, 0x44};
inline const QColor AccentPressed{0xB9, 0x1C, 0x1C};
inline const QColor Warning{0xF5, 0x9E, 0x0B};

constexpr int Unit = 8;
constexpr int RowHeight = 48;
constexpr int Radius = 6;
} // namespace Giuray::Theme

// Papéis adicionais esperados no modelo (ajuste aos papéis já existentes em GameListItem).
namespace Giuray::Roles {
enum : int {
    ItemType = Qt::UserRole + 1,  // GameListItemType: Game, CustomDir, SdmcDir, UserNandDir, SysNandDir, AddDir, Favorites
    DirPath,                      // QString — caminho completo do diretório
    DirGameCount,                 // int — número de jogos no diretório (-1 se desconhecido)
    DirExists,                    // bool — false → marcador âmbar "Not found"
    GameSubtitle,                 // QString — ex. "Last played today · Update 2.0.0"
    AddOnEntries,                 // QStringList — cada entrada "Nome|detalhe|enabled(0/1)"
};
}

class GameListDelegate final : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameListDelegate(QObject* parent = nullptr);

    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;

private:
    void paintBackground(QPainter* p, const QStyleOptionViewItem& opt, bool directory) const;
    void paintDirectory(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void paintAddDirectory(QPainter* p, const QStyleOptionViewItem& opt) const;
    void paintGameName(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void paintFileType(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void paintPlainCell(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void paintAddOns(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const;
    void paintPlaceholderIcon(QPainter* p, const QRect& r, const QString& title) const;

    QFont font(int px, QFont::Weight w) const;
};

// Pinta ações marcadas com property("danger") em vermelho no QMenu
// (o QSS não consegue estilizar itens individuais de QMenu).
#include <QProxyStyle>
class GiurayStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawControl(ControlElement el, const QStyleOption* opt, QPainter* p,
                     const QWidget* w) const override;
    int pixelMetric(PixelMetric m, const QStyleOption* opt, const QWidget* w) const override;
};
