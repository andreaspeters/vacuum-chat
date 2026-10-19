#include "meshcorebledevicecatalog.h"

#include <iostream>

int main()
{
    MeshCoreBleDeviceCatalog catalog;
    int failures = 0;
    const auto check = [&failures](bool condition, const char *message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };

    check(catalog.addDevice(QStringLiteral("MeshCore-Andreas (6AP)"),
                            QStringLiteral("10:bd:a3:5a:6b:e9"), true),
          "a named MeshCore BLE device is accepted");
    check(catalog.devices().size() == 1,
          "accepted device is available to the account form");
    const QList<MeshCoreBleDevice> devices = catalog.devices();
    if (!devices.isEmpty()) {
        const MeshCoreBleDevice &device = devices.first();
        check(device.address == QStringLiteral("10:BD:A3:5A:6B:E9"),
              "selected address is normalized and remains distinct from its label");
        check(device.displayName.contains(QStringLiteral("MeshCore-Andreas (6AP)")) &&
              device.displayName.contains(device.address),
              "picker label identifies the device and shows its address");
        check(device.displayName != device.address,
              "display label is not used as the connection address");
    }
    check(!catalog.addDevice(QStringLiteral("Other BLE device"),
                             QStringLiteral("01:02:03:04:05:06"), true),
          "unrelated BLE devices are not offered as MeshCore radios");
    check(!catalog.addDevice(QStringLiteral("MeshCore radio"),
                             QStringLiteral("01:02:03:04:05:07"), false),
          "non-BLE devices are rejected");
    check(!catalog.addDevice(QStringLiteral("MeshCore radio"),
                             QStringLiteral("not-an-address"), true),
          "devices without a usable address are rejected");
    check(!catalog.addDevice(QStringLiteral("MeshCore duplicate"),
                             QStringLiteral("10:BD:A3:5A:6B:E9"), true),
          "duplicate BLE addresses are listed only once");
    check(catalog.devices().size() == 1,
          "rejected and duplicate devices do not alter the available choices");

    MeshCoreBleAddressSelection retainedAddress(QStringLiteral("AA:BB:CC:DD:EE:FF"));
    check(retainedAddress.address() == QStringLiteral("AA:BB:CC:DD:EE:FF"),
          "the saved BLE address is retained when the form opens");
    check(!retainedAddress.select(QString()),
          "an empty picker selection does not clear the saved BLE address");
    check(retainedAddress.address() == QStringLiteral("AA:BB:CC:DD:EE:FF"),
          "the saved BLE address remains available without a new scan selection");
    check(retainedAddress.select(QStringLiteral("10:BD:A3:5A:6B:E9")),
          "a selected scanner result replaces the saved BLE address");
    check(retainedAddress.address() == QStringLiteral("10:BD:A3:5A:6B:E9"),
          "the selected scanner address is the value to persist");
    check(MeshCoreBleAddressSelection().address() == QStringLiteral("10:BD:A3:5A:6B:E9"),
          "new MeshCore accounts retain the existing default BLE address");

    if (failures == 0)
        std::cout << "MeshCore BLE device catalog tests passed\n";
    return failures == 0 ? 0 : 1;
}
