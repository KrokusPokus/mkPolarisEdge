/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "breezesettingsprovider.h"


namespace MkPolarisEdge
{
SettingsProvider *SettingsProvider::s_self = nullptr;

//__________________________________________________________________
SettingsProvider::SettingsProvider()
    : m_config(KSharedConfig::openConfig(QStringLiteral("mkpolarisedgerc")))
{
    reconfigure();
}

//__________________________________________________________________
SettingsProvider::~SettingsProvider()
{
    s_self = nullptr;
}

//__________________________________________________________________
SettingsProvider *SettingsProvider::self()
{
    // TODO: this is not thread safe!
    if (!s_self) {
        s_self = new SettingsProvider();
    }

    return s_self;
}

//__________________________________________________________________
void SettingsProvider::reconfigure()
{
    if (!m_defaultSettings) {
        m_defaultSettings = InternalSettingsPtr(new InternalSettings());
        m_defaultSettings->setCurrentGroup(QStringLiteral("Windeco"));
    }

    loadSettings(*m_defaultSettings);
}

//__________________________________________________________________
InternalSettingsPtr SettingsProvider::internalSettings(Decoration *) const
{
    // mkPolarisEdge: no window specific exceptions (they could only change the border size and hide the title bar)
    return m_defaultSettings;
}

}
