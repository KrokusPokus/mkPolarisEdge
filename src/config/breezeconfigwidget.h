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

namespace MkPolarisEdge
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
    //* show the custom color button only for the custom highlight color; "None" is the master switch of the highlight:
    // it disables width and radius, the width shows 0 then (the real width is kept in m_highlightWidth)
    void updateHighlightControls();

    //* the highlight width to save: the spin box value, or the kept one while it shows the 0 of "None"
    int highlightWidth() const;

    //* whether the ui shows other values than settings
    bool differsFrom(const InternalSettings &settings) const;

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

    //* highlight width while the spin box shows 0 for "None"
    int m_highlightWidth = 1;
};

}
