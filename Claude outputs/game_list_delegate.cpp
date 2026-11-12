// SPDX-License-Identifier: GPL-2.0-or-later
// Giuray — tema "giuray": pintura da lista onde o QSS não alcança.
#include "game_list_delegate.h"

#include <QApplication>
#include <QFontMetrics>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStandardItem>
#include <QStyleOptionMenuItem>
#include <QTreeView>

using namespace Giuray;
namespace T = Giuray::Theme;

// Colunas atuais da GameList — mantidas sem alteração.
enum Column { ColName = 0, ColFileType, ColSize, ColPlayTime, ColAddOns };
// Tipos de item atuais da GameList.
enum ItemKind { KindGame = QStandardItem::UserType + 1, KindCustomDir, KindSdmcDir,
                KindUserNandDir, KindSysNandDir, KindAddDir, KindFavorites };

static bool isDirectoryKind(int k) {
    return k == KindCustomDir || k == KindSdmcDir || k == KindUserNandDir ||
           k == KindSysNandDir || k == KindFavorites;
}

GameListDelegate::GameListDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QFont GameListDelegate::font(int px, QFont::Weight w) const {
    QFont f(QStringLiteral("Inter"));
    f.setPixelSize(px);
    f.setWeight(w);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QSize GameListDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
    const int kind = idx.data(Roles::ItemType).toInt();
    if (kind != KindGame)
        return {opt.rect.width(), T::RowHeight};

    // Jogo: ícone de 64 px + 16 px de respiro em cima e embaixo (até 2 linhas de chips cabem)
    return {opt.rect.width(), 64 + 2 * 16 + 8};
}

void GameListDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt,
                             const QModelIndex& idx) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);

    const int kind = idx.data(Roles::ItemType).toInt();

    if (kind == KindAddDir) {
        paintBackground(p, opt, false);
        if (idx.column() == ColName) paintAddDirectory(p, opt);
    } else if (isDirectoryKind(kind)) {
        paintBackground(p, opt, true);
        if (idx.column() == ColName) paintDirectory(p, opt, idx);
    } else {
        paintBackground(p, opt, false);
        switch (idx.column()) {
        case ColName:     paintGameName(p, opt, idx); break;
        case ColFileType: paintFileType(p, opt, idx); break;
        case ColAddOns:   paintAddOns(p, opt, idx);   break;
        default:          paintPlainCell(p, opt, idx); break;
        }
    }
    p->restore();
}

void GameListDelegate::paintBackground(QPainter* p, const QStyleOptionViewItem& opt,
                                       bool directory) const {
    const bool hover = opt.state & QStyle::State_MouseOver;
    const bool selected = opt.state & QStyle::State_Selected;

    QColor bg = directory ? T::Surface : T::Window;
    if (hover) bg = T::Elevated;
    if (selected) bg = T::Elevated;
    p->fillRect(opt.rect, bg);

    // Seleção: barra fina de destaque à esquerda (só na 1ª coluna)
    if (selected && opt.viewItemPosition != QStyleOptionViewItem::Middle &&
        opt.viewItemPosition != QStyleOptionViewItem::End) {
        p->fillRect(QRect(opt.rect.left(), opt.rect.top(), 2, opt.rect.height()), T::Accent);
    }
    // Separador sutil
    p->setPen(QPen(T::Border, 1));
    p->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
}

static void drawFolderIcon(QPainter* p, const QRectF& r, const QColor& c) {
    // Ícone de traço simples 20 px, mesmo estilo dos SVGs do tema
    QPainterPath path;
    const qreal x = r.x() + 2, y = r.y() + 4, w = r.width() - 4, h = r.height() - 8;
    path.moveTo(x, y + 2);
    path.quadTo(x, y, x + 2, y);
    path.lineTo(x + w * 0.35, y);
    path.lineTo(x + w * 0.45, y + 2);
    path.lineTo(x + w - 2, y + 2);
    path.quadTo(x + w, y + 2, x + w, y + 4);
    path.lineTo(x + w, y + h - 2);
    path.quadTo(x + w, y + h, x + w - 2, y + h);
    path.lineTo(x + 2, y + h);
    path.quadTo(x, y + h, x, y + h - 2);
    path.closeSubpath();
    p->setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
}

void GameListDelegate::paintDirectory(QPainter* p, const QStyleOptionViewItem& opt,
                                      const QModelIndex& idx) const {
    // A coluna 0 de um diretório ocupa a linha inteira (setFirstColumnSpanned(row, parent, true))
    QRect r = opt.rect.adjusted(T::Unit * 3, 0, -T::Unit * 3, 0);
    const bool exists = idx.data(Roles::DirExists).isValid() ? idx.data(Roles::DirExists).toBool() : true;

    // Chevron de expandir/recolher (o branch nativo fica oculto no QSS)
    const auto* view = qobject_cast<const QTreeView*>(opt.widget);
    const bool expanded = view && view->isExpanded(idx);
    p->setPen(QPen(T::Muted, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF c(r.left() + 8, r.center().y() + 0.5);
    if (expanded) {
        p->drawPolyline(QPolygonF{{c.x() - 4, c.y() - 2}, {c.x(), c.y() + 2}, {c.x() + 4, c.y() - 2}});
    } else {
        p->drawPolyline(QPolygonF{{c.x() - 2, c.y() - 4}, {c.x() + 2, c.y()}, {c.x() - 2, c.y() + 4}});
    }
    r.setLeft(r.left() + T::Unit * 3);

    drawFolderIcon(p, QRectF(r.left(), r.center().y() - 10, 20, 20), exists ? T::Secondary : T::Muted);
    r.setLeft(r.left() + 20 + T::Unit * 1.5);

    // Nome
    const QString name = idx.data(Qt::DisplayRole).toString();
    p->setFont(font(14, QFont::DemiBold));
    p->setPen(exists ? T::Title : T::Secondary);
    QFontMetrics fmName(p->font());
    p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, name);
    r.setLeft(r.left() + fmName.horizontalAdvance(name) + T::Unit * 2);

    // Contagem à direita
    const int count = idx.data(Roles::DirGameCount).toInt();
    const QString countText = (!exists || count < 0) ? QStringLiteral("— games")
                              : (count == 1 ? tr("1 game") : tr("%1 games").arg(count));
    p->setFont(font(12, QFont::Normal));
    p->setPen(T::Secondary);
    QFontMetrics fm12(p->font());
    const int countW = fm12.horizontalAdvance(countText);
    p->drawText(QRect(r.right() - countW, r.top(), countW, r.height()),
                Qt::AlignVCenter | Qt::AlignRight, countText);

    // Caminho (elidido no meio) + marcador "Not found"
    const QString badge = tr("Not found");
    const int badgeW = exists ? 0 : fm12.horizontalAdvance(badge) + 12 + 16;
    const int pathMax = r.width() - countW - badgeW - T::Unit * 4;
    const QString path = fm12.elidedText(idx.data(Roles::DirPath).toString(), Qt::ElideMiddle, pathMax);
    p->setPen(T::Muted);
    p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, path);

    if (!exists) {
        const int bx = r.left() + fm12.horizontalAdvance(path) + T::Unit * 2;
        QFont bf = font(11, QFont::Medium);
        QFontMetrics fmb(bf);
        const QRectF badgeRect(bx, r.center().y() - 9, fmb.horizontalAdvance(badge) + 12 + 14, 18);
        QColor border = T::Warning; border.setAlpha(90);
        p->setPen(QPen(border, 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(badgeRect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        // ícone "!" circular 10 px
        p->setPen(QPen(T::Warning, 1.2));
        const QRectF ic(badgeRect.left() + 6, badgeRect.center().y() - 5, 10, 10);
        p->drawEllipse(ic);
        p->drawLine(QPointF(ic.center().x(), ic.top() + 2.5), QPointF(ic.center().x(), ic.center().y() + 1));
        p->drawPoint(QPointF(ic.center().x(), ic.bottom() - 2.5));
        p->setFont(bf);
        p->drawText(badgeRect.adjusted(20, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, badge);
    }
}

void GameListDelegate::paintAddDirectory(QPainter* p, const QStyleOptionViewItem& opt) const {
    const bool hover = opt.state & QStyle::State_MouseOver;
    const QRectF box(opt.rect.left() + 28, opt.rect.center().y() - 12, 24, 24);
    QPen dashed(hover ? T::Secondary : T::BorderStrong, 1, Qt::DashLine);
    p->setPen(dashed);
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), T::Radius, T::Radius);
    p->setPen(QPen(hover ? T::Text : T::Secondary, 1.5, Qt::SolidLine, Qt::RoundCap));
    p->drawLine(QPointF(box.center().x(), box.top() + 7), QPointF(box.center().x(), box.bottom() - 7));
    p->drawLine(QPointF(box.left() + 7, box.center().y()), QPointF(box.right() - 7, box.center().y()));

    p->setFont(font(13, QFont::Normal));
    p->setPen(hover ? T::Text : T::Secondary);
    p->drawText(opt.rect.adjusted(int(box.right()) - opt.rect.left() + 12, 0, 0, 0),
                Qt::AlignVCenter | Qt::AlignLeft, tr("Add game directory"));
}

void GameListDelegate::paintPlaceholderIcon(QPainter* p, const QRect& r, const QString& title) const {
    p->setPen(QPen(T::BorderStrong, 1));
    p->setBrush(T::Elevated);
    p->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), T::Radius, T::Radius);
    QString initial;
    for (const QChar ch : title) {
        if (ch.isLetterOrNumber()) { initial = ch.toUpper(); break; }
    }
    p->setFont(font(r.height() >= 40 ? 20 : 14, QFont::DemiBold));
    p->setPen(T::Secondary);
    p->drawText(r, Qt::AlignCenter, initial);
}

void GameListDelegate::paintGameName(QPainter* p, const QStyleOptionViewItem& opt,
                                     const QModelIndex& idx) const {
    // Recuo para alinhar com o nome do diretório (24 + 24 + 16 = 64 px)
    QRect r = opt.rect.adjusted(72, 0, -T::Unit * 2, 0);
    const int iconSize = qBound(24, opt.decorationSize.height(), 64);
    const QRect iconRect(r.left(), r.center().y() - iconSize / 2, iconSize, iconSize);

    const QVariant deco = idx.data(Qt::DecorationRole);
    const QString title = idx.data(Qt::DisplayRole).toString().section(QLatin1Char('\n'), 0, 0);
    QPixmap pm;
    if (deco.canConvert<QPixmap>()) pm = deco.value<QPixmap>();
    else if (deco.canConvert<QIcon>()) pm = deco.value<QIcon>().pixmap(iconRect.size());

    if (!pm.isNull()) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(iconRect), T::Radius, T::Radius);
        p->save();
        p->setClipPath(clip);
        p->drawPixmap(iconRect, pm);
        p->restore();
        p->setPen(QPen(QColor(255, 255, 255, 20), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(iconRect).adjusted(0.5, 0.5, -0.5, -0.5), T::Radius, T::Radius);
    } else {
        paintPlaceholderIcon(p, iconRect, title);
    }

    QRect textRect(iconRect.right() + T::Unit * 2, iconRect.top(), r.right() - iconRect.right() - 12, iconSize);
    const QString subtitle = idx.data(Roles::GameSubtitle).toString();

    QFont tf = font(14, QFont::DemiBold);
    QFont sf = font(12, QFont::Normal);
    QFontMetrics fmt(tf), fms(sf);
    const int block = fmt.height() + (subtitle.isEmpty() ? 0 : 4 + fms.height());
    int y = textRect.top() + (textRect.height() - block) / 2;

    p->setFont(tf);
    p->setPen(T::Title);
    p->drawText(QRect(textRect.left(), y, textRect.width(), fmt.height()), Qt::AlignLeft | Qt::AlignVCenter,
                fmt.elidedText(title, Qt::ElideRight, textRect.width()));
    if (!subtitle.isEmpty()) {
        y += fmt.height() + 4;
        p->setFont(sf);
        p->setPen(T::Secondary);
        p->drawText(QRect(textRect.left(), y, textRect.width(), fms.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fms.elidedText(subtitle, Qt::ElideRight, textRect.width()));
    }
}

void GameListDelegate::paintFileType(QPainter* p, const QStyleOptionViewItem& opt,
                                     const QModelIndex& idx) const {
    const QString text = idx.data(Qt::DisplayRole).toString();
    if (text.isEmpty()) return;
    QFont f = font(11, QFont::Medium);
    QFontMetrics fm(f);
    const QRectF tag(opt.rect.left() + 8, opt.rect.center().y() - 10, fm.horizontalAdvance(text) + 12, 20);
    p->setPen(QPen(T::BorderStrong, 1));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(tag.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p->setFont(f);
    p->setPen(T::Text);
    p->drawText(tag, Qt::AlignCenter, text);
}

void GameListDelegate::paintPlainCell(QPainter* p, const QStyleOptionViewItem& opt,
                                      const QModelIndex& idx) const {
    // Tamanho e tempo de jogo: números tabulares, alinhados ao título do jogo
    QFont f = font(13, QFont::Normal);
    f.setFeature(QFont::Tag("tnum"), 1);
    p->setFont(f);
    p->setPen(T::Secondary);
    p->drawText(QRect(opt.rect.left() + 8, opt.rect.center().y() - 10, opt.rect.width() - 16, 20),
                Qt::AlignLeft | Qt::AlignVCenter, idx.data(Qt::DisplayRole).toString());
}

void GameListDelegate::paintAddOns(QPainter* p, const QStyleOptionViewItem& opt,
                                   const QModelIndex& idx) const {
    // Formato: "Nome|detalhe|1"  (1 = ativo, 0 = substituído/desativado)
    QStringList entries = idx.data(Roles::AddOnEntries).toStringList();
    if (entries.isEmpty()) {
        // Fallback: converte o texto original "Nome (detalhe)\n..." sem perder informação.
        for (const QString& line : idx.data(Qt::DisplayRole).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const int paren = line.indexOf(QLatin1String(" ("));
            const bool replaced = line.contains(QLatin1String("replaced by"));
            entries << (paren > 0 ? line.left(paren) + QLatin1Char('|') + line.mid(paren + 2).chopped(1)
                                  : line + QLatin1Char('|'))
                           + (replaced ? "|0" : "|1");
        }
    }

    const QFont nameF = font(12, QFont::Medium);
    const QFont detF = font(11, QFont::Medium);
    QFontMetrics fmN(nameF), fmD(detF);
    constexpr int chipH = 24, gap = 6, padX = 8, dot = 6;

    const int left = opt.rect.left() + 8;
    const int right = opt.rect.right() - 8;
    int x = left;
    int y = opt.rect.top() + 16;

    for (const QString& e : entries) {
        const QStringList parts = e.split(QLatin1Char('|'));
        const QString name = parts.value(0);
        QString detail = parts.value(1);
        const bool enabled = parts.value(2, QStringLiteral("1")) == QLatin1String("1");
        // "built-in" é comum a todos: some do chip (mostrado uma vez no tooltip/legenda)
        detail.remove(QLatin1String("built-in")).replace(QLatin1String(") ("), QLatin1String(" · "));
        detail = detail.split(QRegularExpression(QStringLiteral("[,·]\\s*")), Qt::SkipEmptyParts).join(QStringLiteral(" · ")).trimmed();
        if (!enabled) {
            const int r = detail.indexOf(QLatin1String("replaced by"));
            if (r >= 0) detail = detail.mid(r);
        }

        const int w = padX + dot + 6 + fmN.horizontalAdvance(name) +
                      (detail.isEmpty() ? 0 : 6 + fmD.horizontalAdvance(detail)) + padX;
        if (x + w > right && x > left) { x = left; y += chipH + gap; }

        const QRectF chip(x, y, qMin(w, right - x), chipH);
        if (enabled) {
            p->setPen(QPen(T::Border, 1));
            p->setBrush(T::Elevated);
        } else {
            p->setPen(QPen(T::BorderStrong, 1, Qt::DashLine));
            p->setBrush(Qt::NoBrush);
        }
        p->drawRoundedRect(chip.adjusted(0.5, 0.5, -0.5, -0.5), T::Radius, T::Radius);

        p->setPen(Qt::NoPen);
        p->setBrush(enabled ? T::Accent : T::Disabled);
        p->drawEllipse(QRectF(chip.left() + padX, chip.center().y() - dot / 2.0, dot, dot));

        qreal tx = chip.left() + padX + dot + 6;
        p->setFont(nameF);
        p->setPen(enabled ? T::Text : T::Muted);
        p->drawText(QRectF(tx, chip.top(), chip.right() - tx - padX, chipH), Qt::AlignLeft | Qt::AlignVCenter,
                    fmN.elidedText(name, Qt::ElideRight, int(chip.right() - tx - padX)));
        tx += fmN.horizontalAdvance(name) + 6;
        if (!detail.isEmpty() && tx < chip.right() - padX) {
            p->setFont(detF);
            p->setPen(T::Muted);
            p->drawText(QRectF(tx, chip.top(), chip.right() - tx - padX, chipH), Qt::AlignLeft | Qt::AlignVCenter,
                        fmD.elidedText(detail, Qt::ElideRight, int(chip.right() - tx - padX)));
        }
        x += w + gap;
    }
}

// ---------------------------------------------------------------------------
// GiurayStyle
// ---------------------------------------------------------------------------
void GiurayStyle::drawControl(ControlElement el, const QStyleOption* opt, QPainter* p,
                              const QWidget* w) const {
    if (el == CE_MenuItem) {
        if (const auto* mi = qstyleoption_cast<const QStyleOptionMenuItem*>(opt)) {
            const auto* menu = qobject_cast<const QMenu*>(w);
            const QAction* act = menu ? menu->actionAt(mi->rect.center()) : nullptr;
            if (act && act->property("danger").toBool()) {
                QStyleOptionMenuItem copy(*mi);
                copy.palette.setColor(QPalette::Text, T::AccentHover);
                copy.palette.setColor(QPalette::ButtonText, T::AccentHover);
                copy.palette.setColor(QPalette::HighlightedText, T::AccentHover);
                QProxyStyle::drawControl(el, &copy, p, w);
                return;
            }
        }
    }
    QProxyStyle::drawControl(el, opt, p, w);
}

int GiurayStyle::pixelMetric(PixelMetric m, const QStyleOption* opt, const QWidget* w) const {
    switch (m) {
    case PM_SmallIconSize:       return 16;
    case PM_ToolBarIconSize:     return 16;
    case PM_ButtonIconSize:      return 16;
    case PM_ListViewIconSize:    return 48;
    case PM_MenuVMargin:         return 4;
    case PM_MenuHMargin:         return 4;
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing: return 8;
    default:                     return QProxyStyle::pixelMetric(m, opt, w);
    }
}

/* ---------------------------------------------------------------------------
 * Integração (main.cpp / GameList)
 *
 *   QApplication::setStyle(new GiurayStyle(QStyleFactory::create("Fusion")));
 *   QFontDatabase::addApplicationFont(":/giuray/fonts/Inter-Variable.ttf");
 *   QFile qss(":/giuray/giuray.qss"); qss.open(QFile::ReadOnly);
 *   qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
 *
 *   tree_view->setObjectName("GameList");
 *   tree_view->setItemDelegate(new GameListDelegate(tree_view));
 *   tree_view->setMouseTracking(true);                 // hover
 *   tree_view->setUniformRowHeights(false);            // jogos com vários add-ons
 *   tree_view->setIndentation(0);                      // recuo é pintado pelo delegate
 *   tree_view->setRootIsDecorated(false);
 *   tree_view->setAlternatingRowColors(false);
 *   // diretórios ocupam a linha inteira:
 *   tree_view->setFirstColumnSpanned(row, QModelIndex(), true);
 *   // larguras fixas: File type 96, Size 96, Play time 104, Add-ons stretch
 *
 *   // menu de contexto: ação destrutiva em vermelho
 *   QAction* remove = menu.addAction(icon("trash"), tr("Remove…"));
 *   remove->setProperty("danger", true);
 *
 *   // status bar: estado ligado/desligado
 *   frame_gen_label->setProperty("state", enabled ? "on" : "off");
 *   frame_gen_label->style()->unpolish(frame_gen_label);
 *   frame_gen_label->style()->polish(frame_gen_label);
 * ------------------------------------------------------------------------- */
