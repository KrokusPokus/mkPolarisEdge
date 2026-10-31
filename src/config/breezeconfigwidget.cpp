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

namespace MkAuroraeNext
{
//_________________________________________________________
ConfigWidget::ConfigWidget(QObject *parent, const KPluginMetaData &data, const QVariantList & /*args*/)
    : KCModule(parent, data)
    , m_configuration(KSharedConfig::openConfig(QStringLiteral("mkauroraenextrc")))
    , m_changed(false)
{
    m_ui.setupUi(widget());
    m_ui.tabWidget->tabBar()->setExpanding(true);

    // the custom color button keeps its place while hidden: the centered form does not jump
    QSizePolicy colorPolicy = m_ui.frameLineCustomColor->sizePolicy();
    colorPolicy.setRetainSizeWhenHidden(true);
    m_ui.frameLineCustomColor->setSizePolicy(colorPolicy);

    // track ui changes
    connect(m_ui.frameLineColor, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.frameLineColor, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateCustomColorVisibility);
    connect(m_ui.frameLineCustomColor, &KColorButton::changed, this, &ConfigWidget::updateChanged);
    connect(m_ui.frameLineBold, &QAbstractButton::clicked, this, &ConfigWidget::updateChanged);
    connect(m_ui.titleAlignment, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.shadowSize, &QComboBox::currentIndexChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.shadowStrength, &QSpinBox::valueChanged, this, &ConfigWidget::updateChanged);
    connect(m_ui.shadowColor, &KColorButton::changed, this, &ConfigWidget::updateChanged);
    updateCustomColorVisibility();
}

//_________________________________________________________
void ConfigWidget::setUi(const InternalSettings &settings)
{
    m_ui.frameLineColor->setCurrentIndex(settings.frameLineColor());
    m_ui.frameLineCustomColor->setColor(settings.frameLineCustomColor());
    m_ui.frameLineBold->setChecked(settings.frameLineBold());
    m_ui.titleAlignment->setCurrentIndex(settings.titleAlignment());
    m_ui.shadowSize->setCurrentIndex(std::min<int>(settings.shadowSize(), InternalSettings::ShadowVeryLarge));
    m_ui.shadowStrength->setValue(qRound(qreal(settings.shadowStrength() * 100) / 255));
    m_ui.shadowColor->setColor(settings.shadowColor());
    updateCustomColorVisibility();
}

//_________________________________________________________
void ConfigWidget::updateCustomColorVisibility()
{
    m_ui.frameLineCustomColor->setVisible(m_ui.frameLineColor->currentIndex() == InternalSettings::LineCustom);
}

//_________________________________________________________
void ConfigWidget::load()
{
    m_internalSettings = InternalSettingsPtr(new InternalSettings());
    m_internalSettings->load();
    setUi(*m_internalSettings);
    setNeedsSave(false);
}

//_________________________________________________________
void ConfigWidget::save()
{
    m_internalSettings = InternalSettingsPtr(new InternalSettings());
    m_internalSettings->load();

    m_internalSettings->setFrameLineColor(m_ui.frameLineColor->currentIndex());
    m_internalSettings->setFrameLineCustomColor(m_ui.frameLineCustomColor->color());
    m_internalSettings->setFrameLineBold(m_ui.frameLineBold->isChecked());
    m_internalSettings->setTitleAlignment(m_ui.titleAlignment->currentIndex());
    m_internalSettings->setShadowSize(m_ui.shadowSize->currentIndex());
    m_internalSettings->setShadowStrength(qRound(qreal(m_ui.shadowStrength->value() * 255) / 100));
    m_internalSettings->setShadowColor(m_ui.shadowColor->color());

    m_internalSettings->save();
    m_configuration->sync();
    setNeedsSave(false);

    // needed to tell kwin to reload when running from external kcmshell
    {
        QDBusMessage message = QDBusMessage::createSignal("/KWin", "org.kde.KWin", "reloadConfig");
        QDBusConnection::sessionBus().send(message);
    }

    // needed for the decoration to reload shadows
    {
        QDBusMessage message(QDBusMessage::createSignal("/MkAuroraeNextDecoration", "org.kde.MkAuroraeNext.Style", "reparseConfiguration"));
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

    const bool modified = m_ui.frameLineColor->currentIndex() != m_internalSettings->frameLineColor()
        || m_ui.frameLineCustomColor->color() != m_internalSettings->frameLineCustomColor()
        || m_ui.frameLineBold->isChecked() != m_internalSettings->frameLineBold()
        || m_ui.titleAlignment->currentIndex() != m_internalSettings->titleAlignment()
        || m_ui.shadowSize->currentIndex() != m_internalSettings->shadowSize()
        || qRound(qreal(m_ui.shadowStrength->value() * 255) / 100) != m_internalSettings->shadowStrength()
        || m_ui.shadowColor->color() != m_internalSettings->shadowColor();

    setNeedsSave(modified);
}

}
