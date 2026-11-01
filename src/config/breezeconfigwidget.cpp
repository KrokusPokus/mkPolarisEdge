//////////////////////////////////////////////////////////////////////////////
// breezeconfigurationui.cpp
// -------------------
//
// SPDX-FileCopyrightText: 2009 Hugo Pereira Da Costa <hugo.pereira@free.fr>
//
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////

#include "breezeconfigwidget.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalBlocker>

namespace MkPolarisEdge
{
//_________________________________________________________
ConfigWidget::ConfigWidget(QObject *parent, const KPluginMetaData &data, const QVariantList & /*args*/)
    : KCModule(parent, data)
    , m_configuration(KSharedConfig::openConfig(QStringLiteral("mkpolarisedgerc")))
    , m_changed(false)
{
    m_ui.setupUi(widget());
    m_ui.tabWidget->tabBar()->setExpanding(true);

    // the custom color button keeps its place while hidden: the centered form does not jump
    QSizePolicy colorPolicy = m_ui.highlightCustomColor->sizePolicy();
    colorPolicy.setRetainSizeWhenHidden(true);
    m_ui.highlightCustomColor->setSizePolicy(colorPolicy);

    // track ui changes
    connect(m_ui.activeHighlight, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.activeHighlight, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateHighlightControls);
    connect(m_ui.highlightCustomColor, &KColorButton::changed, this, &ConfigWidget::updateChanged);
    connect(m_ui.highlightWidth, &QSpinBox::valueChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.highlightRadius, &QSpinBox::valueChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.titleAlignment, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.shadowSize, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.shadowStrength, &QSpinBox::valueChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.shadowColor, &KColorButton::changed, this, &ConfigWidget::updateChanged);
    updateHighlightControls();
}

//_________________________________________________________
void ConfigWidget::setUi(const InternalSettings &settings)
{
    m_ui.activeHighlight->setCurrentIndex(settings.activeHighlight());
    m_ui.highlightCustomColor->setColor(settings.activeHighlightCustomColor());
    {
        // the real width; updateHighlightControls() shows 0 instead for "None"
        const QSignalBlocker blocker(m_ui.highlightWidth);
        m_ui.highlightWidth->setMinimum(1);
        m_ui.highlightWidth->setValue(settings.activeHighlightWidth());
    }
    m_ui.highlightRadius->setValue(settings.activeHighlightRadius());
    m_ui.titleAlignment->setCurrentIndex(settings.titleAlignment());
    m_ui.shadowSize->setCurrentIndex(std::min<int>(settings.shadowSize(), InternalSettings::ShadowVeryLarge));
    m_ui.shadowStrength->setValue(qRound(qreal(settings.shadowStrength() * 100) / 255));
    m_ui.shadowColor->setColor(settings.shadowColor());
    updateHighlightControls();
}

//_________________________________________________________
void ConfigWidget::updateHighlightControls()
{
    m_ui.highlightCustomColor->setVisible(m_ui.activeHighlight->currentIndex() == InternalSettings::HighlightCustom);

    // "None" is the master switch: the regular frame is drawn, width and radius do not apply. The width spin box then
    // shows 0 (its minimum is 0 only in this state) and keeps the real width for switching the highlight on again.
    const bool highlight = m_ui.activeHighlight->currentIndex() != InternalSettings::HighlightNone;
    const bool showsNone = m_ui.highlightWidth->minimum() == 0;
    {
        const QSignalBlocker blocker(m_ui.highlightWidth);
        if (!highlight && !showsNone) {
            m_highlightWidth = m_ui.highlightWidth->value();
            m_ui.highlightWidth->setMinimum(0);
            m_ui.highlightWidth->setValue(0);
        } else if (highlight && showsNone) {
            m_ui.highlightWidth->setMinimum(1);
            m_ui.highlightWidth->setValue(m_highlightWidth);
        }
    }
    m_ui.highlightWidthLabel->setEnabled(highlight);
    m_ui.highlightWidth->setEnabled(highlight);
    m_ui.highlightRadiusLabel->setEnabled(highlight);
    m_ui.highlightRadius->setEnabled(highlight);
}

//_________________________________________________________
int ConfigWidget::highlightWidth() const
{
    return m_ui.highlightWidth->minimum() == 0 ? m_highlightWidth : m_ui.highlightWidth->value();
}

//_________________________________________________________
void ConfigWidget::load()
{
    m_internalSettings = InternalSettingsPtr(new InternalSettings());
    loadSettings(*m_internalSettings);
    setUi(*m_internalSettings);
    setNeedsSave(false);
}

//_________________________________________________________
void ConfigWidget::save()
{
    m_internalSettings = InternalSettingsPtr(new InternalSettings());
    m_internalSettings->load();

    m_internalSettings->setActiveHighlight(m_ui.activeHighlight->currentIndex());
    m_internalSettings->setActiveHighlightCustomColor(m_ui.highlightCustomColor->color());
    m_internalSettings->setActiveHighlightWidth(highlightWidth());
    m_internalSettings->setActiveHighlightRadius(m_ui.highlightRadius->value());
    m_internalSettings->setTitleAlignment(m_ui.titleAlignment->currentIndex());
    m_internalSettings->setShadowSize(m_ui.shadowSize->currentIndex());
    m_internalSettings->setShadowStrength(qRound(qreal(m_ui.shadowStrength->value() * 255) / 100));
    m_internalSettings->setShadowColor(m_ui.shadowColor->color());

    m_internalSettings->save();
    // ActiveHighlightBold of version 1.0 is replaced by ActiveHighlightWidth (see loadSettings)
    KConfigGroup(m_internalSettings->config(), QStringLiteral("Windeco")).deleteEntry("ActiveHighlightBold");
    m_internalSettings->config()->sync();
    m_configuration->sync();
    setNeedsSave(false);

    // needed to tell kwin to reload when running from external kcmshell
    {
        QDBusMessage message = QDBusMessage::createSignal("/KWin", "org.kde.KWin", "reloadConfig");
        QDBusConnection::sessionBus().send(message);
    }

    // needed for the decoration to reload shadows
    {
        QDBusMessage message(QDBusMessage::createSignal("/MkPolarisEdgeDecoration", "org.kde.MkPolarisEdge.Style", "reparseConfiguration"));
        QDBusConnection::sessionBus().send(message);
    }
}

//_________________________________________________________
void ConfigWidget::defaults()
{
    m_internalSettings = InternalSettingsPtr(new InternalSettings());
    m_internalSettings->setDefaults();
    setUi(*m_internalSettings);
    updateChanged();
}

//_______________________________________________
void ConfigWidget::updateChanged()
{
    if (!m_internalSettings) {
        return;
    }

    const bool modified = m_ui.activeHighlight->currentIndex() != m_internalSettings->activeHighlight()
        || m_ui.highlightCustomColor->color() != m_internalSettings->activeHighlightCustomColor()
        || highlightWidth() != m_internalSettings->activeHighlightWidth()
        || m_ui.highlightRadius->value() != m_internalSettings->activeHighlightRadius()
        || m_ui.titleAlignment->currentIndex() != m_internalSettings->titleAlignment()
        || m_ui.shadowSize->currentIndex() != m_internalSettings->shadowSize()
        || qRound(qreal(m_ui.shadowStrength->value() * 255) / 100) != m_internalSettings->shadowStrength()
        || m_ui.shadowColor->color() != m_internalSettings->shadowColor();

    setNeedsSave(modified);
}

}
