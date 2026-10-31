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
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>
#include <QTimer>

K_PLUGIN_FACTORY_WITH_JSON(MkAuroraeNextDecoFactory, "mkauroraenext.json", registerPlugin<MkAuroraeNext::Decoration>(); registerPlugin<MkAuroraeNext::Button>();)

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
    case MkAuroraeNext::InternalSettings::ShadowNone:
        return s_shadowParams[0];
    case MkAuroraeNext::InternalSettings::ShadowSmall:
        return s_shadowParams[1];
    case MkAuroraeNext::InternalSettings::ShadowMedium:
        return s_shadowParams[2];
    case MkAuroraeNext::InternalSettings::ShadowLarge:
        return s_shadowParams[3];
    case MkAuroraeNext::InternalSettings::ShadowVeryLarge:
        return s_shadowParams[4];
    default:
        // Fallback to the Large size.
        return s_shadowParams[3];
    }
}
}

namespace MkAuroraeNext
{
using KDecoration3::ColorGroup;
using KDecoration3::ColorRole;

//________________________________________________________________
static int g_sDecoCount = 0;
static int g_shadowSizeEnum = InternalSettings::ShadowLarge;
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
    if (hideTitleBar()) {
        return window()->color(ColorGroup::Inactive, ColorRole::TitleBar);
    } else if (m_animation->state() == QAbstractAnimation::Running) {
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
    if (m_animation->state() == QAbstractAnimation::Running) {
        return KColorUtils::mix(window()->color(ColorGroup::Inactive, ColorRole::Foreground),
                                window()->color(ColorGroup::Active, ColorRole::Foreground),
                                m_opacity);
    } else {
        return window()->color(window()->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::Foreground);
    }
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
    // mkAuroraeNext: the title bar (move area) is everything of the decoration inside the visible frame. The frame
    // around it (left, right, the strip above) is the resize area, so it is exactly what can be seen: windows
    // snapped edge to edge do not reach into each other, and a window at the top screen edge can still be resized.
    const qreal scale = window()->nextScale();
    const qreal x = borderLeft();
    const qreal y = isTopEdge() ? 0 : borderSize(false, scale);
    const qreal width = window()->width();   // the window is the client area; the title bar spans it
    setTitleBar(QRectF(x, y, width, titleAreaHeight(scale)));
}

qreal Decoration::titleAreaHeight(qreal scale) const
{
    return KDecoration3::snapToPixelGrid(Metrics::TitleBar_EdgeTop + Metrics::TitleBar_Height + Metrics::TitleBar_EdgeBottom, scale);
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
    // mkAuroraeNext: the frame of the mkAurorae themes, independent of the border size setting of KWin
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

    qreal top = 0;
    if (hideTitleBar()) {
        top = bottom;
    } else {
        // mkAuroraeNext: frame strip above the title bar (resize area) + title area of the mkAurorae themes
        top = (isTopEdge() ? 0 : borderSize(false, scale)) + titleAreaHeight(scale);
    }
    return QMarginsF(left, top, right, bottom);
}

void Decoration::recalculateBorders()
{
    setBorders(bordersFor(window()->nextScale()));

    // mkAuroraeNext: no resize area outside of the visible frame (Breeze extends it by largeSpacing)
    setResizeOnlyBorders(QMarginsF());

    const bool isTiled = isTopEdge() || isLeftEdge() || isBottomEdge() || isRightEdge();

    qreal topLeftRadius = 0;
    qreal topRightRadius = 0;
    qreal bottomLeftRadius = 0;
    qreal bottomRightRadius = 0;
    if (!isTiled && hasNoBorders() && m_internalSettings->roundedCorners()) {
        if (!isBottomEdge()) {
            if (!isLeftEdge()) {
                bottomLeftRadius = m_scaledCornerRadius;
            }
            if (!isRightEdge()) {
                bottomRightRadius = m_scaledCornerRadius;
            }
        }
        if (hideTitleBar()) {
            if (!isTopEdge()) {
                if (!isLeftEdge()) {
                    topLeftRadius = m_scaledCornerRadius;
                }
                if (!isRightEdge()) {
                    topRightRadius = m_scaledCornerRadius;
                }
            }
        }
    }
    setBorderRadius(KDecoration3::BorderRadius(topLeftRadius, topRightRadius, bottomRightRadius, bottomLeftRadius));

    // mkAuroraeNext: no outline. KWin draws it outside of the decoration, where it would be visible but not a resize
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
    // mkAuroraeNext: buttons TitleHeight high, TitleEdgeTop below the top of the title bar, TitleEdgeLeft / TitleEdgeRight
    // away from the frame, no spacing between them (ButtonSpacing=0 in the mkAurorae themes)
    const qreal scale = window()->scale();
    const auto buttonList = m_leftButtons->buttons() + m_rightButtons->buttons();
    for (KDecoration3::DecorationButton *button : buttonList) {
        auto btn = static_cast<Button *>(button);
        btn->setGeometry(QRectF(QPointF(0, 0), btn->preferredSize()));
        btn->setPadding(QMargins());
    }

    const qreal y = KDecoration3::snapToPixelGrid(titleBar().top() + Metrics::TitleBar_EdgeTop, scale);

    if (!m_leftButtons->buttons().isEmpty()) {
        m_leftButtons->setSpacing(0);
        m_leftButtons->setPos(QPointF(KDecoration3::snapToPixelGrid(borderLeft() + Metrics::TitleBar_EdgeLeft, scale), y));
    }

    if (!m_rightButtons->buttons().isEmpty()) {
        m_rightButtons->setSpacing(0);
        const qreal x = size().width() - borderRight() - Metrics::TitleBar_EdgeRight - m_rightButtons->geometry().width();
        m_rightButtons->setPos(QPointF(KDecoration3::snapToPixelGrid(x, scale), y));
    }

    update();
}

//________________________________________________________________
void Decoration::paint(QPainter *painter, const QRectF &repaintRegion)
{
    // TODO: optimize based on repaintRegion
    auto s = settings();
    const bool isTiled = isTopEdge() || isLeftEdge() || isBottomEdge() || isRightEdge();
    // paint background
    if (!window()->isShaded()) {
        painter->fillRect(rect(), Qt::transparent);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(window()->color(window()->isActive() ? ColorGroup::Active : ColorGroup::Inactive, ColorRole::Frame));

        // clip away the top part
        if (!hideTitleBar()) {
            painter->setClipRect(QRectF(0, borderTop(), size().width(), size().height() - borderTop()), Qt::IntersectClip);
        }

        if (s->isAlphaChannelSupported()) {
            if (hasNoBorders() || isTiled) {
                painter->drawRoundedRect(rect(), 0, 0);
            } else {
                painter->drawRoundedRect(rect(), m_scaledCornerRadius, m_scaledCornerRadius);
            }
        } else {
            painter->drawRect(rect());
        }

        painter->restore();
    }

    if (!hideTitleBar()) {
        paintTitleBar(painter, repaintRegion);
    }

    if (hasBorders() && !s->isAlphaChannelSupported()) {
        const QColor borderColor = borderOutline().color();
        if (borderColor.alphaF() > 0) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setBrush(Qt::NoBrush);
            painter->setPen(borderColor);
            painter->drawRect(rect().adjusted(0, 0, -1, -1));
            painter->restore();
        }
    }
}

//________________________________________________________________
void Decoration::paintTitleBar(QPainter *painter, const QRectF &repaintRegion)
{
    QRectF rect(QPointF(0, 0), QSizeF(size().width(), borderTop()));
    QBrush frontBrush;
    QBrush backBrush(this->titleBarColor());
    const bool isTiled = isTopEdge() || isLeftEdge() || isBottomEdge() || isRightEdge();

    if (!rect.intersects(repaintRegion)) {
        return;
    }

    painter->save();
    painter->setPen(Qt::NoPen);

    // render a linear gradient on title area
    if (window()->isActive() && m_internalSettings->drawBackgroundGradient()) {
        QLinearGradient gradient(0, 0, 0, rect.height());
        gradient.setColorAt(0.0, titleBarColor().lighter(120));
        gradient.setColorAt(0.8, titleBarColor());

        frontBrush = gradient;

    } else {
        frontBrush = titleBarColor();

        painter->setBrush(titleBarColor());
    }

    if (isMaximized() || isTiled || !settings()->isAlphaChannelSupported()) {
        painter->setBrush(backBrush);
        painter->drawRect(rect);

        painter->setBrush(frontBrush);
        painter->drawRect(rect);
    } else if (window()->isShaded()) {
        painter->setBrush(backBrush);
        painter->drawRoundedRect(rect, m_scaledCornerRadius, m_scaledCornerRadius);

        painter->setBrush(frontBrush);
        painter->drawRoundedRect(rect, m_scaledCornerRadius, m_scaledCornerRadius);

    } else {
        painter->setClipRect(rect, Qt::IntersectClip);

        auto drawThe = [this, painter](const QRectF &r) {
            painter->drawRoundedRect(r, m_scaledCornerRadius, m_scaledCornerRadius);
            // remove the rounding on the bottom
            painter->drawRect(QRectF(r.bottomLeft() - QPointF(0, m_scaledCornerRadius), r.bottomRight()));
        };

        painter->setBrush(backBrush);
        drawThe(rect);

        painter->setBrush(frontBrush);
        drawThe(rect);
    }

    painter->restore();

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
    // mkAuroraeNext: buttons as high as the title (TitleHeight of the mkAurorae themes)
    return Metrics::TitleBar_Height;
}

//________________________________________________________________
qreal Decoration::captionHeight() const
{
    // mkAuroraeNext: the caption is as high as the title (TitleHeight)
    return hideTitleBar() ? borderTop() : KDecoration3::snapToPixelGrid(Metrics::TitleBar_Height, window()->scale());
}

//________________________________________________________________
QPair<QRectF, Qt::Alignment> Decoration::captionRect() const
{
    if (hideTitleBar()) {
        return qMakePair(QRectF(), Qt::AlignCenter);
    } else {
        const qreal leftOffset =
            KDecoration3::snapToPixelGrid(m_leftButtons->buttons().isEmpty() ? Metrics::TitleBar_SideMargin * settings()->smallSpacing()
                                                                             : m_leftButtons->geometry().x() + m_leftButtons->geometry().width()
                                                  + Metrics::TitleBar_SideMargin * settings()->smallSpacing(),
                                          window()->scale());

        const qreal rightOffset = KDecoration3::snapToPixelGrid(m_rightButtons->buttons().isEmpty() ? Metrics::TitleBar_SideMargin * settings()->smallSpacing()
                                                                                                    : size().width() - m_rightButtons->geometry().x()
                                                                        + Metrics::TitleBar_SideMargin * settings()->smallSpacing(),
                                                                window()->scale());

        const qreal yOffset = KDecoration3::snapToPixelGrid(titleBar().top() + Metrics::TitleBar_EdgeTop, window()->scale());
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
