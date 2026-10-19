#ifndef STATUSSHOWCOMBOHELPER_H
#define STATUSSHOWCOMBOHELPER_H

#include <QComboBox>

#include <interfaces/istatuschanger.h>
#include <interfaces/ipresence.h>

namespace StatusChangerPrivate
{
inline void populateStatusShowCombo(QComboBox *comboBox, IStatusChanger *statusChanger)
{
    const int showValues[] = {
        IPresence::Online,
        IPresence::Chat,
        IPresence::Away,
        IPresence::DoNotDisturb,
        IPresence::ExtendedAway,
        IPresence::Invisible,
        IPresence::Offline
    };
    for (const int show : showValues) {
        comboBox->addItem(statusChanger->iconByShow(show),
                          statusChanger->nameByShow(show),
                          show);
    }
}
}

#endif // STATUSSHOWCOMBOHELPER_H
