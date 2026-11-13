// SPDX-FileCopyrightText: Copyright 2026 Giuray
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QIcon>

/// Small line icons drawn in code, so the library's controls share one stroke style and need no
/// image files.
namespace Glyphs {

enum class Glyph {
    Search,
    Sliders,
    List,
    Grid,
    Carousel,
    Play,
    Star,
    StarFilled,
    More,
    Plus,
    Volume,
};

/// A 16x16 icon: muted at rest, near-white when hovered, checked or selected.
QIcon Icon(Glyph glyph);
/// One pixmap of the glyph in one color (for labels and painted badges).
QPixmap Pixmap(Glyph glyph, const QColor& color, int size = 16);

} // namespace Glyphs
