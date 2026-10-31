/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include "breezesettings.h"

#include <QColor>
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

//* mkAuroraeNext: geometry of the mkAurorae themes (logical pixels, from decoration.svg, the button SVGs and the rc file).
// The resize area is exactly the visible frame: there are no resize borders outside of it.
static constexpr int Frame_BorderWidth = 4;     // left, right, bottom and the strip above the title bar
static constexpr int TitleBar_EdgeTop = 3;      // TitleEdgeTop: from the outer edge to the buttons
static constexpr int TitleBar_ButtonSize = 20;  // Aurorae default button size; the 22 x 22 button SVGs are scaled to it
static constexpr int TitleBar_EdgeBottom = 3;   // TitleEdgeBottom
static constexpr int TitleBar_EdgeLeft = 10;    // TitleEdgeLeft: from the outer window edge to the left buttons
static constexpr int TitleBar_EdgeRight = 5;    // TitleEdgeRight: from the right buttons to the outer window edge
static constexpr int TitleBar_CaptionMargin = 5; // between the buttons and the caption
//* height of the top part of the decoration, measured from the outer edge: 3 + 20 + 3 = 26
static constexpr int TitleBar_TotalHeight = TitleBar_EdgeTop + TitleBar_ButtonSize + TitleBar_EdgeBottom;
//* maximized windows (TitleEdgeTopMaximized=-1, TitleEdgeBottomMaximized=1; left and right measured on the Aurorae
// rendering): buttons cut off by one pixel at the top, the first and the last one right at the edges
static constexpr int TitleBar_EdgeTopMaximized = -1;
static constexpr int TitleBar_EdgeBottomMaximized = 1;
static constexpr int TitleBar_EdgeLeftMaximized = 0;
static constexpr int TitleBar_EdgeRightMaximized = 0;
static constexpr int TitleBar_TotalHeightMaximized = TitleBar_EdgeTopMaximized + TitleBar_ButtonSize + TitleBar_EdgeBottomMaximized;
}

//* mkAuroraeNext: colors of the mkAurorae themes (decoration.svg)
namespace Theme
{
inline const QColor Rim(0x22, 0x22, 0x22);          // outermost pixel
inline const QColor Rim_Top(0x27, 0x27, 0x27);      // its top row; in the top corners a gradient Rim_Top -> Rim over 26 px
inline const QColor Inner(0x24, 0x24, 0x24);        // pixels 2 and 3 of the frame
inline const QColor Title_Light(0x35, 0x35, 0x35);  // title gradient, active: light at the top, inactive: reversed
inline const QColor Title_Dark(0x24, 0x24, 0x24);
inline const QColor Title_Maximized(0x35, 0x35, 0x35);   // maximized: plain, no gradient, no separators
inline const QColor Separator(0x2c, 0x2c, 0x2c);    // row 24
inline const QColor Separator_Active(0x21, 0x21, 0x21);   // row 25
inline const QColor Separator_Inactive(0x3c, 0x3c, 0x3c);
inline const QColor Line_Inactive(0x50, 0x50, 0x50);      // frame line of inactive windows (and of the gray variant)
inline const QColor Line_InactiveTop(0x5e, 0x5e, 0x5e);   // its top row
inline const QColor Line_Purple(0x85, 0x00, 0xf7);
inline const QColor Line_Red(0xff, 0x00, 0x00);
inline const QColor Text_Active(255, 255, 255);     // ActiveTextColor
inline const QColor Text_Inactive(128, 128, 128);   // InactiveTextColor
static constexpr qreal Corner_OuterRadius = 3;              // frame line corners of the active window: outside ...
static constexpr qreal Corner_OuterRadiusBold = 4;          // bold variant: line pieces scaled by 4/3 ...
static constexpr qreal Corner_OuterRadiusBoldTop = 3 * 1.04; // ... the top corners vertically only by 1.04
static constexpr qreal Corner_InnerRadius = 2;              // ... and inside
}

namespace Metrics
{
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
