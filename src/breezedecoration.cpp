/*
 * SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 * SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
 * SPDX-FileCopyrightText: 2021 Paul McAuley <kde@paulmcauley.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "breezedecoration.h"

#include "breezesettingsprovider.h"

#include "breezebutton.h"

#include "breezeboxshadowrenderer.h"

#include <KDecoration3/DecorationButtonGroup>
#include <KDecoration3/DecorationShadow>
#include <KDecoration3/ScaleHelpers>

#include <KColorUtils>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>

#include <KColorScheme>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QTimer>
#include <QTransform>

#include <array>
#include <cmath>

K_PLUGIN_FACTORY_WITH_JSON(MkPolarisEdgeDecoFactory, "mkpolarisedge.json", registerPlugin<MkPolarisEdge::Decoration>(); registerPlugin<MkPolarisEdge::Button>();)

namespace
{
struct ShadowParams {
    ShadowParams()
        : offset(QPoint(0, 0))
        , radius(0)
        , opacity(0)
    {
    }

    ShadowParams(const QPoint &offset, int radius, qreal opacity)
        : offset(offset)
        , radius(radius)
        , opacity(opacity)
    {
    }

    QPoint offset;
    int radius;
    qreal opacity;
};

struct CompositeShadowParams {
    CompositeShadowParams() = default;

    CompositeShadowParams(const QPoint &offset, const ShadowParams &shadow1, const ShadowParams &shadow2)
        : offset(offset)
        , shadow1(shadow1)
        , shadow2(shadow2)
    {
    }

    bool isNone() const
    {
        return qMax(shadow1.radius, shadow2.radius) == 0;
    }

    QPoint offset;
    ShadowParams shadow1;
    ShadowParams shadow2;
};

const CompositeShadowParams s_shadowParams[] = {
    // None
    CompositeShadowParams(),
    // Small
    CompositeShadowParams(QPoint(0, 4), ShadowParams(QPoint(0, 0), 16, 1), ShadowParams(QPoint(0, -2), 8, 0.4)),
    // Medium
    CompositeShadowParams(QPoint(0, 8), ShadowParams(QPoint(0, 0), 32, 0.9), ShadowParams(QPoint(0, -4), 16, 0.3)),
    // Large
    CompositeShadowParams(QPoint(0, 12), ShadowParams(QPoint(0, 0), 48, 0.8), ShadowParams(QPoint(0, -6), 24, 0.2)),
    // Very large
    CompositeShadowParams(QPoint(0, 16), ShadowParams(QPoint(0, 0), 64, 0.7), ShadowParams(QPoint(0, -8), 32, 0.1)),
};

inline CompositeShadowParams lookupShadowParams(int size)
{
    switch (size) {
    case MkPolarisEdge::InternalSettings::ShadowNone:
        return s_shadowParams[0];
    case MkPolarisEdge::InternalSettings::ShadowSmall:
        return s_shadowParams[1];
    case MkPolarisEdge::InternalSettings::ShadowMedium:
        return s_shadowParams[2];
    case MkPolarisEdge::InternalSettings::ShadowLarge:
        return s_shadowParams[3];
    case MkPolarisEdge::InternalSettings::ShadowVeryLarge:
        return s_shadowParams[4];
    default:
        // Fallback to the Large size.
        return s_shadowParams[3];
    }
}

// mkPolarisEdge: vertical gradient for painting the frame
QBrush verticalGradient(qreal y1, qreal y2, const QColor &c1, const QColor &c2)
{
    QLinearGradient g(0, y1, 0, y2);
    g.setColorAt(0, c1);
    g.setColorAt(1, c2);
    return QBrush(g);
}

// mkPolarisEdge: QPainter blends the gamma-encoded values, which makes mixed pixels of a light line over a dark
// background too dark. The arcs of the line are therefore blended in linear light (see paintLine()) into small
// images, one per corner, which only have to be made once per key.
struct CornerKey {
    QRgb line;
    int R;
    int width;
    int active;
    int mirrorX;
    int mirrorY;
    // all of them in 1/256: the scale, the fractional part of the corner in device pixels, the window size up to
    // where it can still reach into the corner
    int scaleX;
    int scaleY;
    int offsetX;
    int offsetY;
    int W;
    int H;

    bool operator==(const CornerKey &) const = default;
};

size_t qHash(const CornerKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.line, k.R, k.width, k.active, k.mirrorX, k.mirrorY, k.scaleX, k.scaleY, k.offsetX, k.offsetY, k.W, k.H);
}

struct CornerTile {
    QImage image;
    QPoint offset; // of the image, relative to the device pixel the corner of the window lies in
};

constexpr int s_cornerTileLimit = 64; // far above any real use; only guards against unlimited growth
static QHash<CornerKey, CornerTile> g_sCornerTiles;

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c)
{
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

const std::array<float, 256> &srgbToLinearTable()
{
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t;
        for (int i = 0; i < 256; ++i) {
            t[i] = srgbToLinear(i / 255.0f);
        }
        return t;
    }();
    return table;
}

// mixes line over background with the coverage of mask, in linear light. All pixels with coverage 0 or 255 stay
// exact, because the conversion of 8 bit values there and back is free of loss.
void blendLinear(QImage &background, const QImage &mask, const QColor &line)
{
    const auto &toLinear = srgbToLinearTable();
    const QRgb lineRgb = line.rgb();
    const float lineR = toLinear[qRed(lineRgb)];
    const float lineG = toLinear[qGreen(lineRgb)];
    const float lineB = toLinear[qBlue(lineRgb)];
    const float lineA = line.alphaF();

    for (int y = 0; y < background.height(); ++y) {
        auto *dst = reinterpret_cast<QRgb *>(background.scanLine(y));
        const uchar *cov = mask.constScanLine(y);
        for (int x = 0; x < background.width(); ++x) {
            if (cov[x] == 0) {
                continue;
            }
            const QRgb bg = qUnpremultiply(dst[x]);
            const float a = cov[x] / 255.0f * lineA;
            const float bgA = qAlpha(bg) / 255.0f * (1.0f - a);
            const float outA = a + bgA;
            if (outA <= 0.0f) {
                continue;
            }
            const auto mix = [&](float l, int b) {
                const float c = (a * l + bgA * toLinear[b]) / outA;
                return qBound(0, qRound(linearToSrgb(c) * 255.0f), 255);
            };
            dst[x] = qPremultiply(qRgba(mix(lineR, qRed(bg)), mix(lineG, qGreen(bg)), mix(lineB, qBlue(bg)), qRound(outA * 255.0f)));
        }
    }
}
}

namespace MkPolarisEdge
{
using KDecoration3::ColorGroup;
using KDecoration3::ColorRole;

//________________________________________________________________
static int g_sDecoCount = 0;
static int g_shadowSizeEnum = InternalSettings::ShadowNone;
static int g_shadowStrength = 255;
static QColor g_shadowColor = Qt::black;
static std::shared_ptr<KDecoration3::DecorationShadow> g_sShadow;
static std::shared_ptr<KDecoration3::DecorationShadow> g_sShadowInactive;

//________________________________________________________________
Decoration::Decoration(QObject *parent, const QVariantList &args)
    : KDecoration3::Decoration(parent, args)
    , m_animation(new QVariantAnimation(this))
    , m_shadowAnimation(new QVariantAnimation(this))
{
    g_sDecoCount++;
}

//________________________________________________________________
Decoration::~Decoration()
{
    g_sDecoCount--;
    if (g_sDecoCount == 0) {
        // last deco destroyed, clean up shadow
        g_sShadow.reset();
        g_sCornerTiles.clear();
    }
}

//________________________________________________________________
void Decoration::setOpacity(qreal value)
{
    if (m_opacity == value) {
        return;
    }
    m_opacity = value;
    update();
}

//________________________________________________________________
QColor Decoration::titleBarColor() const
{
    if (m_animation->state() == QAbstractAnimation::Running) {
        return KColorUtils::mix(window()->color(ColorGroup::Inactive, ColorRole::TitleBar),
                                window()->color(ColorGroup::Active, ColorRole::TitleBar),
                                m_opacity);
    } else {
        return window()->color(window()->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::TitleBar);
    }
}

//________________________________________________________________
QColor Decoration::fontColor() const
{
    // mkPolarisEdge: ActiveTextColor / InactiveTextColor of the theme
    if (m_animation->state() == QAbstractAnimation::Running) {
        return KColorUtils::mix(Theme::Text_Inactive, Theme::Text_Active, m_opacity);
    }
    return window()->isActive() ? Theme::Text_Active : Theme::Text_Inactive;
}

//________________________________________________________________
bool Decoration::init()
{
    // active state change animation
    // It is important start and end value are of the same type, hence 0.0 and not just 0
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    // Linear to have the same easing as Breeze animations
    m_animation->setEasingCurve(QEasingCurve::Linear);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setOpacity(value.toReal());
    });

    m_shadowAnimation->setStartValue(0.0);
    m_shadowAnimation->setEndValue(1.0);
    m_shadowAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_shadowAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_shadowOpacity = value.toReal();
        updateShadow();
    });

    // use DBus connection to update on breeze configuration change
    auto dbus = QDBusConnection::sessionBus();
    dbus.connect(QString(),
                 QStringLiteral("/KGlobalSettings"),
                 QStringLiteral("org.kde.KGlobalSettings"),
                 QStringLiteral("notifyChange"),
                 this,
                 SLOT(reconfigure()));

    reconfigure();
    updateTitleBar();
    auto s = settings();
    connect(s.get(), &KDecoration3::DecorationSettings::borderSizeChanged, this, &Decoration::recalculateBorders);

    // a change in font might cause the borders to change
    connect(s.get(), &KDecoration3::DecorationSettings::fontChanged, this, &Decoration::recalculateBorders);
    connect(s.get(), &KDecoration3::DecorationSettings::spacingChanged, this, &Decoration::recalculateBorders);

    // buttons
    connect(s.get(), &KDecoration3::DecorationSettings::spacingChanged, this, &Decoration::updateButtonsGeometryDelayed);
    connect(s.get(), &KDecoration3::DecorationSettings::decorationButtonsLeftChanged, this, &Decoration::updateButtonsGeometryDelayed);
    connect(s.get(), &KDecoration3::DecorationSettings::decorationButtonsRightChanged, this, &Decoration::updateButtonsGeometryDelayed);

    // full reconfiguration
    connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, this, &Decoration::reconfigure);
    connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, SettingsProvider::self(), &SettingsProvider::reconfigure, Qt::UniqueConnection);
    connect(s.get(), &KDecoration3::DecorationSettings::reconfigured, this, &Decoration::updateButtonsGeometryDelayed);

    connect(window(), &KDecoration3::DecoratedWindow::activeChanged, this, &Decoration::recalculateBorders);
    connect(window(), &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::recalculateBorders);
    connect(window(), &KDecoration3::DecoratedWindow::maximizedHorizontallyChanged, this, &Decoration::recalculateBorders);
    connect(window(), &KDecoration3::DecoratedWindow::maximizedVerticallyChanged, this, &Decoration::recalculateBorders);
    connect(window(), &KDecoration3::DecoratedWindow::shadedChanged, this, &Decoration::recalculateBorders);
    connect(window(), &KDecoration3::DecoratedWindow::captionChanged, this, [this]() {
        // update the caption area
        update(titleBar());
    });

    connect(window(), &KDecoration3::DecoratedWindow::activeChanged, this, &Decoration::updateAnimationState);
    connect(this, &KDecoration3::Decoration::bordersChanged, this, &Decoration::updateTitleBar);
    connect(this, &KDecoration3::Decoration::bordersChanged, this, &Decoration::updateButtonsGeometry);
    connect(window(), &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::updateTitleBar);
    connect(window(), &KDecoration3::DecoratedWindow::widthChanged, this, &Decoration::updateTitleBar);
    connect(window(), &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::updateTitleBar);
    connect(window(), &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::setOpaque);

    connect(window(), &KDecoration3::DecoratedWindow::widthChanged, this, &Decoration::updateButtonsGeometry);
    connect(window(), &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::updateButtonsGeometry);
    connect(window(), &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::updateButtonsGeometry);
    connect(window(), &KDecoration3::DecoratedWindow::shadedChanged, this, &Decoration::updateButtonsGeometry);

    connect(window(), &KDecoration3::DecoratedWindow::nextScaleChanged, this, &Decoration::updateScale);

    createButtons();
    updateShadow();
    return true;
}

//________________________________________________________________
void Decoration::updateTitleBar()
{
    // mkPolarisEdge: the title bar (move area) is everything of the decoration inside the visible frame. The frame
    // around it (left, right, the strip above) is the resize area, so it is exactly what can be seen: windows
    // snapped edge to edge do not reach into each other, and a window at the top screen edge can still be resized.
    const qreal scale = window()->nextScale();
    const qreal y = isTopEdge() ? 0 : borderSize(false, scale);
    setTitleBar(QRectF(borderLeft(), y, window()->width(), borderTop() - y));
}

//________________________________________________________________
void Decoration::updateAnimationState()
{
    if (m_shadowAnimation->duration() > 0) {
        m_shadowAnimation->setDirection(window()->isActive() ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
        m_shadowAnimation->setEasingCurve(window()->isActive() ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
        if (m_shadowAnimation->state() != QAbstractAnimation::Running) {
            m_shadowAnimation->start();
        }

    } else {
        updateShadow();
    }

    if (m_animation->duration() > 0) {
        m_animation->setDirection(window()->isActive() ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
        if (m_animation->state() != QAbstractAnimation::Running) {
            m_animation->start();
        }

    } else {
        update();
    }
}

//________________________________________________________________
qreal Decoration::borderSize(bool bottom, qreal scale) const
{
    // mkPolarisEdge: the frame of the mkAurorae themes, independent of the border size setting of KWin
    Q_UNUSED(bottom);
    return KDecoration3::snapToPixelGrid(Metrics::Frame_BorderWidth, scale);
}

//________________________________________________________________
void Decoration::reconfigure()
{
    m_internalSettings = SettingsProvider::self()->internalSettings(this);

    setScaledCornerRadius();

    // animation

    KSharedConfig::Ptr config = KSharedConfig::openConfig();
    const KConfigGroup cg(config, QStringLiteral("KDE"));

    m_animation->setDuration(0);
    // Syncing anis between client and decoration is troublesome, so we're not using
    // any animations right now.
    // m_animation->setDuration( cg.readEntry("AnimationDurationFactor", 1.0f) * 100.0f );

    // But the shadow is fine to animate like this!
    m_shadowAnimation->setDuration(cg.readEntry("AnimationDurationFactor", 1.0f) * 100.0f);

    // borders
    recalculateBorders();

    // shadow
    updateShadow();
}

QMarginsF Decoration::bordersFor(qreal scale) const
{
    const qreal left = isLeftEdge() ? 0 : borderSize(false, scale);
    const qreal right = isRightEdge() ? 0 : borderSize(false, scale);
    const qreal bottom = (window()->isShaded() || isBottomEdge()) ? 0 : borderSize(true, scale);

    // mkPolarisEdge: TitleEdgeTop + button size + TitleEdgeBottom from the outer edge; the frame strip (resize area)
    // is the top part of it
    const qreal top = KDecoration3::snapToPixelGrid(isMaximized() ? Metrics::TitleBar_TotalHeightMaximized : Metrics::TitleBar_TotalHeight, scale);
    return QMarginsF(left, top, right, bottom);
}

void Decoration::recalculateBorders()
{
    setBorders(bordersFor(window()->nextScale()));

    // mkPolarisEdge: no resize area outside of the visible frame (Breeze extends it by largeSpacing)
    setResizeOnlyBorders(QMarginsF());

    // mkPolarisEdge: the window is rectangular, the frame is always there (Breeze rounds windows without borders)
    setBorderRadius(KDecoration3::BorderRadius());

    // mkPolarisEdge: no outline. KWin draws it outside of the decoration, where it would be visible but not a resize
    // area; the frame of the mkAurorae themes has its colored edge inside the 4 px border.
    setBorderOutline(KDecoration3::BorderOutline());
}

//________________________________________________________________
void Decoration::createButtons()
{
    m_leftButtons = new KDecoration3::DecorationButtonGroup(KDecoration3::DecorationButtonGroup::Position::Left, this, &Button::create);
    m_rightButtons = new KDecoration3::DecorationButtonGroup(KDecoration3::DecorationButtonGroup::Position::Right, this, &Button::create);
    updateButtonsGeometry();
}

//________________________________________________________________
void Decoration::updateButtonsGeometryDelayed()
{
    QTimer::singleShot(0, this, &Decoration::updateButtonsGeometry);
}

//________________________________________________________________
void Decoration::updateButtonsGeometry()
{
    // mkPolarisEdge: buttons 20 x 20 (the 22 x 22 button SVGs scaled like Aurorae does), TitleEdgeTop below the outer
    // edge, the first one TitleEdgeLeft from the outer window edge, the last one TitleEdgeRight before it, no spacing
    // (ButtonSpacing=0). Note: they reach one pixel into the frame strip above the title bar, as in Aurorae.
    const qreal scale = window()->scale();
    const auto buttonList = m_leftButtons->buttons() + m_rightButtons->buttons();
    for (KDecoration3::DecorationButton *button : buttonList) {
        auto btn = static_cast<Button *>(button);
        btn->setGeometry(QRectF(0, 0, Metrics::TitleBar_ButtonSize, Metrics::TitleBar_ButtonSize));
        btn->setPadding(QMargins());
    }

    const bool maximized = isMaximized();
    const qreal y = KDecoration3::snapToPixelGrid(maximized ? Metrics::TitleBar_EdgeTopMaximized : Metrics::TitleBar_EdgeTop, scale);

    if (!m_leftButtons->buttons().isEmpty()) {
        m_leftButtons->setSpacing(0);
        const qreal x = maximized ? Metrics::TitleBar_EdgeLeftMaximized : Metrics::TitleBar_EdgeLeft;
        m_leftButtons->setPos(QPointF(KDecoration3::snapToPixelGrid(x, scale), y));
    }

    if (!m_rightButtons->buttons().isEmpty()) {
        m_rightButtons->setSpacing(0);
        const qreal edge = maximized ? Metrics::TitleBar_EdgeRightMaximized : Metrics::TitleBar_EdgeRight;
        const qreal x = size().width() - edge - m_rightButtons->geometry().width();
        m_rightButtons->setPos(QPointF(KDecoration3::snapToPixelGrid(x, scale), y));
    }

    update();
}

//________________________________________________________________
void Decoration::paint(QPainter *painter, const QRectF &repaintRegion)
{
    // mkPolarisEdge: frame and title background of the mkAurorae themes, piece by piece as in decoration.svg.
    // Two layers: the frame background without any line, then exactly one line over it: the colored highlight of
    // the active window, or else the gray line (inactive windows, and HighlightNone). Both lines are painted by
    // paintLine(); nothing of the gray line remains below the highlight, so its rounded corners show the background.
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (isMaximized()) {
        // maximized: the title area is plain (measured on the Aurorae rendering of decoration-maximized-center)
        painter->fillRect(QRectF(0, 0, size().width(), borderTop()), Theme::Title_Maximized);
    } else {
        paintFrame(painter, size().width(), size().height(), window()->isActive());
        if (window()->isActive() && m_internalSettings->activeHighlight() != InternalSettings::HighlightNone) {
            paintLine(painter, highlightColor(), m_internalSettings->activeHighlightWidth(), m_internalSettings->activeHighlightRadius());
        } else {
            // the gray mkAurorae theme: thin line, square corners, its top row lighter
            paintLine(painter, Theme::Line_Inactive, 1, 0);
            painter->fillRect(QRectF(1, 1, size().width() - 2, 1), Theme::Line_InactiveTop);
        }
    }
    painter->restore();

    paintTitleBar(painter, repaintRegion);
}

void Decoration::paintFrame(QPainter *painter, qreal W, qreal H, bool active)
{
    // mkPolarisEdge: the frame background without the line: rim in pixel 0, pixel 1 (where the line goes) as rim
    // too, pixels 2 and 3 inside, title gradient and separators. Square everywhere; the line and its corners are
    // painted over it by paintLine(). Pixel boundaries are integers (logical pixels). Only the title area differs
    // between active and inactive windows.
    const qreal T = Metrics::TitleBar_TotalHeight;   // 26: separators in rows T - 2 and T - 1

    auto fill = [painter](qreal x, qreal y, qreal w, qreal h, const QBrush &brush) {
        painter->fillRect(QRectF(x, y, w, h), brush);
    };

    // title gradient over its area (measured on the Aurorae rendering): active rows 2-23, inactive rows 2-24 (an
    // inactive window has no separator in row 24)
    const QBrush titleBrush =
        active ? verticalGradient(2, T - 2, Theme::Title_Light, Theme::Title_Dark) : verticalGradient(2, T - 1, Theme::Title_Dark, Theme::Title_Light);

    // pixels 0 and 1: rim; in the top corners a gradient over 26 px, the top row lighter
    fill(0, 0, W, H, Theme::Rim);
    fill(4, 0, W - 8, 1, Theme::Rim_Top);
    fill(0, 0, 4, T, verticalGradient(0, T, Theme::Rim_Top, Theme::Rim));
    fill(W - 4, 0, 4, T, verticalGradient(0, T, Theme::Rim_Top, Theme::Rim));

    // pixels 2 and 3: inside
    fill(2, T, 2, H - T - 4, Theme::Inner);
    fill(W - 4, T, 2, H - T - 4, Theme::Inner);
    fill(4, H - 4, W - 8, 2, Theme::Inner);

    // corners: pixels 2 and 3
    fill(2, 2, 2, active ? T - 4 : T - 3, titleBrush);
    fill(W - 4, 2, 2, active ? T - 4 : T - 3, titleBrush);
    fill(2, H - 4, 2, 2, Theme::Inner);
    fill(W - 4, H - 4, 2, 2, Theme::Inner);

    // inside the top corner pieces below the title gradient
    if (active) {
        fill(2, T - 2, 2, 2, Theme::Inner);
        fill(W - 4, T - 2, 2, 2, Theme::Inner);
    } else {
        fill(2, T - 1, 2, 1, Theme::Separator_Inactive);
        fill(W - 4, T - 1, 2, 1, Theme::Separator_Inactive);
    }

    // title gradient and the separator rows between the corner pieces
    fill(4, 2, W - 8, active ? T - 4 : T - 3, titleBrush);
    if (active) {
        fill(4, T - 2, W - 8, 1, Theme::Separator);
    }
    fill(4, T - 1, W - 8, 1, active ? Theme::Separator_Active : Theme::Separator_Inactive);
}

void Decoration::paintLine(QPainter *painter, const QColor &line, int width, qreal R)
{
    // mkPolarisEdge: the line of the frame, painted over the background of paintFrame(): the gray line (width 1,
    // radius 0) or the highlight of the active window (the color variants of the mkAurorae themes, settings
    // ActiveHighlightWidth and ActiveHighlightRadius). It covers width pixel rows of the 4 px frame: width 1 is the
    // thin line in pixel 1, 2 the bold line ("B" themes) in pixels 0 and 1, 3 and 4 grow one row further in each.
    // Its corners are concentric arcs. R is the radius at pixel edge 0, the outer edge of the frame (also when the
    // line starts at pixel 1); every pixel edge further in has a radius 1 smaller, down to 0 (default 4: bold outer
    // 4, thin outer 3, inner 2, as in the mkAurorae themes; 0: square corners).
    const qreal W = size().width();
    const qreal H = size().height();
    const qreal o = width == 1 ? 1 : 0;       // outer edge of the line
    const qreal i = width == 1 ? 2 : width;   // inner edge of the line
    const qreal ro = qMax<qreal>(0, R - o);                              // radius of the outer edge
    const qreal ri = qMax<qreal>(0, R - i);                              // radius of the inner edge
    const qreal c = qMax(R, i);   // the corner pieces reach from the outer edge to here, the straight pieces start here

    // corner piece top left, between the outer and the inner arc; mirrored for the other corners
    QPainterPath corner;
    corner.moveTo(o, c);
    corner.lineTo(o, o + ro);
    if (ro > 0) {
        corner.arcTo(QRectF(o, o, 2 * ro, 2 * ro), 180, -90);
    }
    corner.lineTo(c, o);
    corner.lineTo(c, i);
    corner.lineTo(i + ri, i);
    if (ri > 0) {
        corner.arcTo(QRectF(i, i, 2 * ri, 2 * ri), 90, 90);
    }
    corner.lineTo(i, c);
    corner.closeSubpath();

    // first the corners: on a fractional scale the pieces share pixels, and an image replaces what lies below it
    if ((ro > 0 || ri > 0) && painter->deviceTransform().type() <= QTransform::TxScale) {
        // arcs: blended in linear light
        paintCornerTile(painter, corner, line, width, R, c, false, false);
        paintCornerTile(painter, corner, line, width, R, c, true, false);
        paintCornerTile(painter, corner, line, width, R, c, false, true);
        paintCornerTile(painter, corner, line, width, R, c, true, true);
    } else {
        // square corners have no mixed pixels (on an integer scale)
        painter->fillPath(corner, line);
        painter->fillPath(QTransform(-1, 0, 0, 1, W, 0).map(corner), line);    // top right
        painter->fillPath(QTransform(1, 0, 0, -1, 0, H).map(corner), line);    // bottom left
        painter->fillPath(QTransform(-1, 0, 0, -1, W, H).map(corner), line);   // bottom right
    }

    // straight pieces: rectangles (much cheaper to rasterize than a path over the whole window height)
    painter->fillRect(QRectF(c, o, W - 2 * c, i - o), line);       // top
    painter->fillRect(QRectF(c, H - i, W - 2 * c, i - o), line);   // bottom
    painter->fillRect(QRectF(o, c, i - o, H - 2 * c), line);       // left
    painter->fillRect(QRectF(W - i, c, i - o, H - 2 * c), line);   // right
}

void Decoration::paintCornerTile(QPainter *painter, const QPainterPath &corner, const QColor &line, int width, qreal R, qreal c, bool mirrorX, bool mirrorY)
{
    // mkPolarisEdge: one corner of the line in linear light: the frame background and the coverage of the corner
    // piece are rasterized by QPainter as usual, only their mix is computed here. Painted in device pixels, so the
    // image lands 1:1 on the pixels it was made for. The window size only matters for very small windows, so it is
    // limited to keep the number of images down.
    const qreal T = Metrics::TitleBar_TotalHeight;
    const qreal limitW = 2 * c + 16;
    const qreal limitH = T + c + 8;
    const qreal W = qMin(size().width(), limitW);
    const qreal H = qMin(size().height(), limitH);

    const QTransform device = painter->deviceTransform();
    const qreal sx = device.m11();
    const qreal sy = device.m22();
    const QPointF anchor = device.map(QPointF(mirrorX ? size().width() : 0, mirrorY ? size().height() : 0));
    const QPoint anchorPixel(qFloor(anchor.x()), qFloor(anchor.y()));
    const qreal ox = anchor.x() - anchorPixel.x();
    const qreal oy = anchor.y() - anchorPixel.y();

    const CornerKey key{line.rgba(),
                        qRound(R),
                        width,
                        window()->isActive(),
                        mirrorX,
                        mirrorY,
                        qRound(sx * 256),
                        qRound(sy * 256),
                        qRound(ox * 256),
                        qRound(oy * 256),
                        qRound(W * 256),
                        qRound(H * 256)};

    auto it = g_sCornerTiles.constFind(key);
    if (it == g_sCornerTiles.constEnd()) {
        // the part of the device pixels the corner reaches, relative to anchorPixel
        const QPointF far(ox + (mirrorX ? -c * sx : c * sx), oy + (mirrorY ? -c * sy : c * sy));
        const QPoint topLeft(qFloor(qMin(ox, far.x())), qFloor(qMin(oy, far.y())));
        const QPoint bottomRight(qCeil(qMax(ox, far.x())), qCeil(qMax(oy, far.y())));
        const QSize tileSize(bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y());

        // from logical coordinates of a window of size W x H to pixels of the image
        const QTransform toTile(sx, 0, 0, sy, ox - topLeft.x() - (mirrorX ? W * sx : 0), oy - topLeft.y() - (mirrorY ? H * sy : 0));

        QImage image(tileSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            p.setPen(Qt::NoPen);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setTransform(toTile);
            paintFrame(&p, W, H, window()->isActive());
        }

        QImage mask(tileSize, QImage::Format_Alpha8);
        mask.fill(0);
        {
            QPainter p(&mask);
            p.setPen(Qt::NoPen);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setTransform(toTile);
            p.fillPath(QTransform(mirrorX ? -1 : 1, 0, 0, mirrorY ? -1 : 1, mirrorX ? W : 0, mirrorY ? H : 0).map(corner), Qt::black);
        }

        blendLinear(image, mask, line);

        if (g_sCornerTiles.size() >= s_cornerTileLimit) {
            g_sCornerTiles.clear();
        }
        it = g_sCornerTiles.insert(key, CornerTile{image, topLeft});
    }

    // in device pixels: neither the window of the painter nor the device pixel ratio apply
    const qreal dpr = painter->device()->devicePixelRatioF();
    painter->save();
    painter->setViewTransformEnabled(false);
    painter->setWorldTransform(QTransform::fromScale(1 / dpr, 1 / dpr));
    painter->drawImage(anchorPixel + it->offset, it->image);
    painter->restore();
}

QColor Decoration::highlightColor() const
{
    // mkPolarisEdge: the highlight line of the active window, the color variants of the mkAurorae themes
    switch (m_internalSettings->activeHighlight()) {
    case InternalSettings::HighlightAccent:   // ColorScheme-ViewFocus in the SVG
        return KColorScheme(QPalette::Active, KColorScheme::View).decoration(KColorScheme::FocusColor).color();
    case InternalSettings::HighlightRed:
        return Theme::Line_Red;
    case InternalSettings::HighlightCustom:
        return m_internalSettings->activeHighlightCustomColor();
    default:
    case InternalSettings::HighlightPurple:
        return Theme::Line_Purple;
    }
}

//________________________________________________________________
void Decoration::paintTitleBar(QPainter *painter, const QRectF &repaintRegion)
{
    // mkPolarisEdge: the background is drawn in paint(); here only the caption and the buttons
    if (!QRectF(QPointF(0, 0), QSizeF(size().width(), borderTop())).intersects(repaintRegion)) {
        return;
    }

    // draw caption
    painter->setFont(settings()->font());
    painter->setPen(fontColor());
    const auto [captionRectangle, alignment] = captionRect();
    const QString caption = painter->fontMetrics().elidedText(window()->caption(), Qt::ElideMiddle, captionRectangle.width());
    painter->drawText(captionRectangle, alignment | Qt::TextSingleLine, caption);

    // draw all buttons
    m_leftButtons->paint(painter, repaintRegion);
    m_rightButtons->paint(painter, repaintRegion);
}

//________________________________________________________________
int Decoration::buttonSize() const
{
    // mkPolarisEdge: Aurorae default button size
    return Metrics::TitleBar_ButtonSize;
}

//________________________________________________________________
qreal Decoration::captionHeight() const
{
    // mkPolarisEdge: the caption is centered in the band of the buttons
    return KDecoration3::snapToPixelGrid(Metrics::TitleBar_ButtonSize, window()->scale());
}

//________________________________________________________________
QPair<QRectF, Qt::Alignment> Decoration::captionRect() const
{
    {
        const qreal leftOffset =
            KDecoration3::snapToPixelGrid(m_leftButtons->buttons().isEmpty() ? Metrics::TitleBar_CaptionMargin
                                                                             : m_leftButtons->geometry().x() + m_leftButtons->geometry().width()
                                                  + Metrics::TitleBar_CaptionMargin,
                                          window()->scale());

        const qreal rightOffset = KDecoration3::snapToPixelGrid(m_rightButtons->buttons().isEmpty() ? Metrics::TitleBar_CaptionMargin
                                                                                                    : size().width() - m_rightButtons->geometry().x()
                                                                        + Metrics::TitleBar_CaptionMargin,
                                                                window()->scale());

        const qreal yOffset = KDecoration3::snapToPixelGrid(isMaximized() ? Metrics::TitleBar_EdgeTopMaximized : Metrics::TitleBar_EdgeTop, window()->scale());
        const QRectF maxRect(leftOffset, yOffset, size().width() - leftOffset - rightOffset, captionHeight());

        switch (m_internalSettings->titleAlignment()) {
        case InternalSettings::AlignLeft:
            return qMakePair(maxRect, Qt::AlignVCenter | Qt::AlignLeft);

        case InternalSettings::AlignRight:
            return qMakePair(maxRect, Qt::AlignVCenter | Qt::AlignRight);

        case InternalSettings::AlignCenter:
            return qMakePair(maxRect, Qt::AlignCenter);

        default:
        case InternalSettings::AlignCenterFullWidth: {
            // full caption rect
            const QRectF fullRect = QRect(0, yOffset, size().width(), captionHeight());
            QRectF boundingRect(settings()->fontMetrics().boundingRect(window()->caption()));

            // text bounding rect
            boundingRect.setTop(yOffset);
            boundingRect.setHeight(captionHeight());
            boundingRect.moveLeft((size().width() - boundingRect.width()) / 2);

            if (boundingRect.left() < leftOffset) {
                return qMakePair(maxRect, Qt::AlignVCenter | Qt::AlignLeft);
            } else if (boundingRect.right() > size().width() - rightOffset) {
                return qMakePair(maxRect, Qt::AlignVCenter | Qt::AlignRight);
            } else {
                return qMakePair(fullRect, Qt::AlignCenter);
            }
        }
        }
    }
}

//________________________________________________________________
void Decoration::updateShadow()
{
    auto s = settings();

    // Animated case, no cached shadow object
    if ((m_shadowAnimation->state() == QAbstractAnimation::Running) && (m_shadowOpacity != 0.0) && (m_shadowOpacity != 1.0)) {
        setShadow(createShadowObject(0.5 + m_shadowOpacity * 0.5));
        return;
    }

    if (g_shadowSizeEnum != m_internalSettings->shadowSize() || g_shadowStrength != m_internalSettings->shadowStrength()
        || g_shadowColor != m_internalSettings->shadowColor()) {
        g_sShadow.reset();
        g_sShadowInactive.reset();
        g_shadowSizeEnum = m_internalSettings->shadowSize();
        g_shadowStrength = m_internalSettings->shadowStrength();
        g_shadowColor = m_internalSettings->shadowColor();
    }

    auto &shadow = (window()->isActive()) ? g_sShadow : g_sShadowInactive;
    if (!shadow) {
        g_sShadow = createShadowObject(1.0);
        g_sShadowInactive = createShadowObject(0.5);
    }
    setShadow(shadow);
}

//________________________________________________________________
std::shared_ptr<KDecoration3::DecorationShadow> Decoration::createShadowObject(const float strengthScale)
{
    CompositeShadowParams params = lookupShadowParams(m_internalSettings->shadowSize());
    if (params.isNone()) {
        // If shadows are disabled, return nothing
        return nullptr;
    }

    auto withOpacity = [](const QColor &color, qreal opacity) -> QColor {
        QColor c(color);
        c.setAlphaF(opacity);
        return c;
    };

    const QSize boxSize =
        BoxShadowRenderer::calculateMinimumBoxSize(params.shadow1.radius).expandedTo(BoxShadowRenderer::calculateMinimumBoxSize(params.shadow2.radius));

    BoxShadowRenderer shadowRenderer;
    shadowRenderer.setBorderRadius(m_scaledCornerRadius + 0.5);
    shadowRenderer.setBoxSize(boxSize);

    const qreal strength = m_internalSettings->shadowStrength() / 255.0 * strengthScale;
    shadowRenderer.addShadow(params.shadow1.offset, params.shadow1.radius, withOpacity(m_internalSettings->shadowColor(), params.shadow1.opacity * strength));
    shadowRenderer.addShadow(params.shadow2.offset, params.shadow2.radius, withOpacity(m_internalSettings->shadowColor(), params.shadow2.opacity * strength));

    QImage shadowTexture = shadowRenderer.render();

    QPainter painter(&shadowTexture);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF outerRect = shadowTexture.rect();

    QRectF boxRect(QPoint(0, 0), boxSize);
    boxRect.moveCenter(outerRect.center());

    // Mask out inner rect.
    const QMarginsF padding = QMarginsF(boxRect.left() - outerRect.left() - Metrics::Shadow_Overlap - params.offset.x(),
                                        boxRect.top() - outerRect.top() - Metrics::Shadow_Overlap - params.offset.y(),
                                        outerRect.right() - boxRect.right() - Metrics::Shadow_Overlap + params.offset.x(),
                                        outerRect.bottom() - boxRect.bottom() - Metrics::Shadow_Overlap + params.offset.y());
    QRectF innerRect = outerRect - padding;
    // Push the shadow slightly under the window, which helps avoiding glitches with fractional scaling
    // TODO fix this more properly
    innerRect.adjust(2, 2, -2, -2);

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.drawRoundedRect(innerRect, m_scaledCornerRadius + 0.5, m_scaledCornerRadius + 0.5);

    painter.end();

    auto ret = std::make_shared<KDecoration3::DecorationShadow>();
    ret->setPadding(padding);
    ret->setInnerShadowRect(QRectF(outerRect.center(), QSizeF(1, 1)));
    ret->setShadow(shadowTexture);
    return ret;
}

void Decoration::setScaledCornerRadius()
{
    // On X11, the smallSpacing value is used for scaling.
    // On Wayland, this value has constant factor of 2.
    // Removing it will break radius scaling on X11.
    m_scaledCornerRadius = KDecoration3::snapToPixelGrid(Metrics::Frame_FrameRadius * settings()->smallSpacing(), window()->nextScale());
}

void Decoration::updateScale()
{
    setScaledCornerRadius();
    recalculateBorders();
}
} // namespace

#include "breezedecoration.moc"
