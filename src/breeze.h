/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include "breezesettings.h"

#include <QList>
#include <QSharedPointer>

namespace MkAuroraeNext
{
//* convenience typedefs
using InternalSettingsPtr = QSharedPointer<InternalSettings>;
using InternalSettingsList = QList<InternalSettingsPtr>;
using InternalSettingsListIterator = QListIterator<InternalSettingsPtr>;

//* metrics
namespace Metrics
{
//* corner radius, in units of small spacing
// Keep this value in sync with Bias_Default in
// kstyle/breezemetrics.h
// NOTE: this value is multiplied by settings()->smallSpacing()
// which is always 2 on wayland, but can be something else on X11
static constexpr qreal Frame_FrameRadius = 2.5;

//* titlebar metrics, in units of small spacing
static constexpr int TitleBar_TopMargin = 2;
static constexpr int TitleBar_BottomMargin = 2;
static constexpr int TitleBar_SideMargin = 2;
static constexpr int TitleBar_ButtonSpacing = 2;

//* mkAuroraeNext: geometry of the mkAurorae themes (logical pixels, decoration.svg and the theme rc file).
// The resize area is exactly the visible frame: there are no resize borders outside of it.
static constexpr int Frame_BorderWidth = 4;     // left, right, bottom and the strip above the title bar
static constexpr int TitleBar_EdgeTop = 3;      // TitleEdgeTop
static constexpr int TitleBar_Height = 16;      // TitleHeight: height of buttons and caption
static constexpr int TitleBar_EdgeBottom = 3;   // TitleEdgeBottom
static constexpr int TitleBar_EdgeLeft = 10;    // TitleEdgeLeft: from the frame to the left buttons
static constexpr int TitleBar_EdgeRight = 5;    // TitleEdgeRight: from the right buttons to the frame

// shadow dimensions (pixels)
static constexpr int Shadow_Overlap = 3;

// frame intensities (called bias in KColorUtilities::Mix)
// Keep this value in sync with Bias_Default in
// kstyle/breezemetrics.h
static constexpr qreal Bias_Default = 0.20;
}

//* standard pen widths
namespace PenWidth
{
/* Using 1 instead of slightly more than 1 causes symbols drawn with
 * pen strokes to look skewed. The exact amount added does not matter
 * as long as it isn't too visible.
 */
// The standard pen stroke width for symbols.
static constexpr qreal Symbol = 1.01;
}

//* exception
enum ExceptionMask {
    None = 0,
    BorderSize = 1 << 4,
};
}
