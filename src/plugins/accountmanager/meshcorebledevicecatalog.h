#ifndef MESHCOREBLEDEVICECATALOG_H
#define MESHCOREBLEDEVICECATALOG_H

#include <QList>
#include <QRegularExpression>
#include <QString>

struct MeshCoreBleDevice
{
    QString displayName;
    QString address;
};

// Sanitizes the raw discovery results before they are presented by the account form.
class MeshCoreBleDeviceCatalog
{
public:
    bool addDevice(const QString &name, const QString &address, bool isLowEnergy)
    {
        const QString cleanName = name.trimmed();
        const QString cleanAddress = address.trimmed().toUpper();
        static const QRegularExpression macAddressPattern(
            QStringLiteral("^[0-9A-F]{2}(:[0-9A-F]{2}){5}$"));

        if (!isLowEnergy || !cleanName.contains(QStringLiteral("MeshCore"), Qt::CaseInsensitive) ||
            !macAddressPattern.match(cleanAddress).hasMatch())
            return false;

        for (const MeshCoreBleDevice &device : m_devices)
            if (device.address.compare(cleanAddress, Qt::CaseInsensitive) == 0)
                return false;

        m_devices.append(MeshCoreBleDevice{
            cleanName + QStringLiteral(" — ") + cleanAddress,
            cleanAddress
        });
        return true;
    }

    void clear()
    {
        m_devices.clear();
    }

    QList<MeshCoreBleDevice> devices() const
    {
        return m_devices;
    }

private:
    QList<MeshCoreBleDevice> m_devices;
};

// Retains the configured address until the user selects a discovered device.
class MeshCoreBleAddressSelection
{
public:
    explicit MeshCoreBleAddressSelection(const QString &storedAddress = QString())
    {
        reset(storedAddress);
    }

    void reset(const QString &storedAddress)
    {
        m_address = storedAddress.trimmed().isEmpty()
            ? QStringLiteral("10:BD:A3:5A:6B:E9") : storedAddress;
    }

    bool select(const QString &address)
    {
        const QString selectedAddress = address.trimmed();
        if (selectedAddress.isEmpty() || selectedAddress == m_address)
            return false;
        m_address = selectedAddress;
        return true;
    }

    QString address() const
    {
        return m_address;
    }

private:
    QString m_address;
};

#endif // MESHCOREBLEDEVICECATALOG_H
