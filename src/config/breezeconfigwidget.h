//////////////////////////////////////////////////////////////////////////////
// breezeconfigurationui.h
// -------------------
//
// SPDX-FileCopyrightText: 2009 Hugo Pereira Da Costa <hugo.pereira@free.fr>
//
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "breeze.h"
#include "breezesettings.h"
#include "ui_breezeconfigurationui.h"

#include <KCModule>
#include <KSharedConfig>

#include <QSharedPointer>
#include <QWidget>

namespace MkAuroraeNext
{
//_____________________________________________
class ConfigWidget : public KCModule
{
    Q_OBJECT

public:
    //* constructor
    explicit ConfigWidget(QObject *parent, const KPluginMetaData &data, const QVariantList &args);

    //* destructor
    virtual ~ConfigWidget() = default;

    //* default
    void defaults() override;

    //* load configuration
    void load() override;

    //* save configuration
    void save() override;

protected Q_SLOTS:

    //* update changed state
    virtual void updateChanged();

private:
    //* show the custom color button only for the custom line color
    void updateCustomColorVisibility();

    //* assigns the settings to the ui
    void setUi(const InternalSettings &settings);

private:
    //* ui
    Ui_BreezeConfigurationUI m_ui;

    //* kconfiguration object
    KSharedConfig::Ptr m_configuration;

    //* settings as loaded or saved last
    InternalSettingsPtr m_internalSettings;

    //* changed state
    bool m_changed;
};

}
