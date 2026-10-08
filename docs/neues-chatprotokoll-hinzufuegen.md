# Neues Chat-Protokoll hinzufügen

Diese Anleitung beschreibt den Erweiterungspfad für ein neues Chat-Protokoll in
Vacuum. Die gemeinsamen Chat-Komponenten konsumieren protokollneutrale
Interfaces; Anmeldung, Netzwerkformat, Identitäten und Übersetzung
eingehender/ausgehender Ereignisse bleiben im jeweiligen Protokolladapter.

## Grundregeln

- **Keine Protokollverzweigungen in der gemeinsamen Chat-UI.**
`ChatMessageHandler` und andere gemeinsame Komponenten dürfen nicht anhand von
`"matrix"`, `"xmpp"` oder einem konkreten Adaptertyp routen. Neue Funktionen
werden über bestehende generische Interfaces oder ein gezieltes
 Capability-Interface angeboten.
- **Protokollnative IDs bleiben im Adapter maßgeblich.**
`ConversationId` muss für Anzeige, Verlauf und Versand stabil dasselbe
Gespräch
 bezeichnen. Keine synthetische JID als autoritative Identität eines
Nicht-XMPP-Gesprächs verwenden.
- **Account-, Stream- und Gesprächs-ID sind unterschiedliche Identitäten.**
`AccountId` bezeichnet die gespeicherte Kontoidentität, `streamId()` die
Provider-/Stream-Zuordnung und `ConversationId` das konkrete Gespräch. Nicht
gleichsetzen; für Fenster- und Ereignisrouting die vom Interface verlangte ID
benutzen.
- **Optionale Fähigkeiten haben sichere Defaults.**
Im gemeinsamen Interface nur hinzufügen, wenn die Funktion
protokollübergreifend
sinnvoll ist. Nicht unterstützte Fähigkeiten müssen `false`, eine leere Liste
oder einen neutralen Wert liefern; nicht als scheinbar aktive, aber wirkungslose
UI-Aktion erscheinen.
- **Keine zweite UI-spezifische Transportimplementierung.**
Datei- und Medienpfade über die vorhandenen generischen Interfaces und
Adaptergrenzen führen. Protokoll-REST/E2EE bleibt im Adapter;
XMPP-Dateitransfers
verwenden den vorhandenen `IFileTransfer`-/Jingle-Pfad.
- **GUI nicht blockieren.** Das bestehende asynchrone
`QNetworkAccessManager`-Muster wiederverwenden. Worker-Objekte besitzen ihren
Zustand und liefern Ergebnisse per Signals/Slots zurück; keinen Thread pro
Netzwerkrequest starten.

Die Regeln folgen der bestehenden Architektur in
`src/interfaces/iprotocolmessaging.h`, `iprotocolroster.h`,
`iprotocolpresence.h`
und `iprotocolcapabilities.h` sowie den Projektvorgaben in `AGENTS.md`.

## Plugin-Dateiaufbau

Ein eigenständiges Protokollplugin folgt dem Muster der Verzeichnisse
`src/plugins/matrix` und `src/plugins/meshcore`:

```text
src/plugins/<protokoll>/
├── CMakeLists.txt # Plugin-Metadaten und Einbindung des Builders
├── <protokoll>.cmake              # SOURCES und HEADERS
├── <protokoll>.h # QObject-Plugin und Interface-Deklarationen
├── <protokoll>.cpp                # Lebenszyklus und Adapterlogik
├── <protokoll>.json # falls Q_PLUGIN_METADATA FILE verwendet wird
└── <funktion>_test.cpp            # fokussierte Regressionstests
```

Die beiden CMake-Dateien haben getrennte Aufgaben:

```cmake
# src/plugins/examplechat/CMakeLists.txt
project(examplechat)

set(PLUGIN_NAME "examplechat")
set(PLUGIN_DISPLAY_NAME "Example Chat")
set(PLUGIN_DEPENDENCIES) # optionale CPack-Abhängigkeiten

include("examplechat.cmake")
include("${CMAKE_SOURCE_DIR}/src/plugins/plugins.cmake")
```

```cmake
# src/plugins/examplechat/examplechat.cmake
set(SOURCES examplechat.cpp)
set(HEADERS examplechat.h)
```

`src/plugins/plugins.cmake` erzeugt bei aktiviertem `PLUGIN_<PLUGIN_NAME>` das
Shared-Library-Target, richtet das Plugin-Ausgabeverzeichnis ein und bindet die
gemeinsamen Qt-Abhängigkeiten ein. Optionale Bibliotheken werden im jeweiligen
Plugin-CMake gesucht und – wie beim Matrix-Plugin – über `ADD_LIBS`
ergänzt. Für
Zusatzdateien und testspezifische Include-/Link-Einstellungen das konkrete
Plugin-CMake prüfen.

Danach das neue Unterverzeichnis in `src/plugins/plugin_list.cmake`
registrieren:

```cmake
add_subdirectory(examplechat)
```

Ohne diesen Eintrag wird das Verzeichnis nicht in der normalen Plugin-Auflistung
konfiguriert. Die reale Registrierung bestehender Protokolle steht am Ende von
`src/plugins/plugin_list.cmake`.

Wenn die Klasse `Q_PLUGIN_METADATA(... FILE "examplechat.json")` verwendet,
muss die referenzierte JSON-Datei mitgeliefert werden. Die Metadaten eines
bestehenden Plugins nicht blind kopieren: UUIDs/Schlüssel müssen für das neue
Plugin eindeutig sein und zur tatsächlichen Verwendung passen.

## Plugin-Klasse und Interfaces

`IPlugin` ist der dynamische Plugin-Lebenszyklus aus
`src/interfaces/ipluginmanager.h`. Ein Plugin stellt `instance()`,
`pluginUuid()`,
`pluginInfo()`, `initConnections()`, `initObjects()`, `initSettings()` und
`startPlugin()` bereit. Qt benötigt außerdem `Q_OBJECT`, `Q_PLUGIN_METADATA`
und
für jedes implementierte Interface einen passenden Eintrag in `Q_INTERFACES`.

Ein minimales Chat- und Capability-Gerüst sieht konzeptionell so aus:

```cpp
#include <QObject>
#include <interfaces/ipluginmanager.h>
#include <interfaces/iprotocolmessaging.h>
#include <interfaces/iprotocolcapabilities.h>

class ExampleChat final : public QObject,
                          public IPlugin,
                          public IProtocolMessaging,
                          public IProtocolCapabilities
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "Vacuum.Core.IPlugin/1.0" FILE "examplechat.json")
    Q_INTERFACES(IPlugin IProtocolMessaging IProtocolCapabilities)

public:
    QObject *instance() override { return this; }
    QUuid pluginUuid() const override { return FUuid; }
    void pluginInfo(IPluginInfo *info) override;
    bool initConnections(IPluginManager *manager, int &initOrder) override;
    bool initObjects() override;
    bool initSettings() override;
    bool startPlugin() override;

    AccountId streamId() const override;
    QString protocol() const override { return QStringLiteral("examplechat"); }
    bool conversationIdForAddress(const Jid &address,
ConversationId &conversationId) const override;
Jid addressForConversation(const ConversationId &conversationId) const override;
    bool sendMessage(const BasicMessage &message) override;

    IProtocolCapabilities::Capabilities capabilitiesForAccount(
        const AccountId &accountId,
        const ConversationId &targetId = ConversationId()) const override;

signals:
    // Der gemeinsame Chat-Handler konsumiert dieses bestehende Signal.
    void protocolMessageReceived(const BasicMessage &message);

private:
    QUuid FUuid; // feste, für dieses Plugin eindeutige Plugin-ID
};
```

Das Gerüst zeigt die Signaturen, ist aber ohne Implementierung des
Netzwerk-/Accountlebenszyklus kein vollständiges Plugin. Die Methoden und Typen
sind in den genannten Interfaces definiert; insbesondere sind bei
`IProtocolMessaging`
`streamId()`, `conversationIdForAddress()`, `addressForConversation()` und
`sendMessage()` verpflichtend. `protocol()` hat dagegen einen neutralen Default,
sollte vom Adapter aber überschrieben werden, damit Nachrichten korrekt dem
Protokoll zugeordnet werden.

### Interfaces gezielt ergänzen

| Interface | Wann implementieren | Wichtige Punkte |
|---|---|---|
| `IProtocolMessaging` | Für Nachrichtenversand und die gemeinsame
Chatintegration | Verpflichtende Identitäts-/Adressübersetzung und
`sendMessage()`. `BasicMessage` am Adapterrand erzeugen bzw. entgegennehmen;
Transportdetails nicht in den Renderer geben. |
| `IProtocolRoster` | Wenn das Protokoll Kontakte, Räume oder Gespräche für
die gemeinsame Rosteransicht anbietet | `accountId()`, `streamId()`,
`entries()`, `rooms()`, `entry()` und `room()` müssen konsistente Snapshots
liefern. Räume bleiben Gespräche mit ihrer nativen `ConversationId`. |
| `IProtocolPresence` | Wenn der Adapter generischen Online-/Offline- und
Statuswechsel unterstützt | Präsenz-Enums und Setzpfad aus
`iprotocolpresence.h` nutzen. Offline muss die tatsächliche Adapter-Session
beenden, nicht nur einen UI-Status setzen. |
| `IProtocolCapabilities` | Für unterstützte Aktionen des Kontos bzw. eines
konkreten Gesprächs | `hasCapabilities()` ist bereits als gemeinsamer Helfer
implementiert. Der Adapter überschreibt `capabilitiesForAccount(accountId,
targetId)` und gibt nur jetzt tatsächlich nutzbare Fähigkeiten zurück. Leere
Capability-Menge bedeutet nicht unterstützt. |
| Weitere spezialisierte Interfaces | Nur für konkrete Funktionen wie
Benachrichtigungen, Profil-/Kontaktaktionen oder Medien | Keine einzelne breite
Capability für voneinander unabhängige Funktionen verwenden. Bestehende
Interface-Defaults zuerst prüfen. |

**Projektkonvention:** Jeder Chat-Protokolladapter soll `IProtocolCapabilities`
anbieten und damit den gemeinsamen `hasCapabilities()`-Pfad abdecken – auch
wenn er für ein Konto oder eine Unterhaltung keine optionalen Aktionen
unterstützt. `hasCapabilities()` selbst ist kein virtuelles Plugin-Override;
implementiert wird `capabilitiesForAccount()`.

`IProtocolRoster` und `IProtocolPresence` sind keine Voraussetzung für den
reinen Messaging-Vertrag. Werden sie eingebunden, müssen die passenden
Basisklassen auch in `Q_INTERFACES` stehen. Die Matrix-Klasse in
`src/plugins/matrix/matrix.h` zeigt ein Plugin, das mehrere dieser Interfaces
direkt implementiert; `src/plugins/meshcore/meshcoreplugin.h` zeigt die
Alternative, Protokoll-/Transportlogik in ein separates Objekt auszulagern.

## Signal- und Ereignisfluss

`IProtocolMessaging` deklariert die Qt-Signale nicht als
C++-Interfacefunktionen. Die gemeinsame Komponente verbindet die QObject-Signale
des Plugin-Objekts nach festem Namen. Der aktuelle
`ChatMessageHandler::startPlugin()` verbindet
`protocolMessageReceived(BasicMessage)` und verbindet optionale Signale nur,
wenn die zugehörige Capability dies ankündigt.

Für neue Adapter gilt:

1. Protokollereignisse in der Adapterebene in `BasicMessage`, generische IDs und
den passenden gemeinsamen Signalvertrag übersetzen.
2. Für optionales Verhalten ein vorhandenes generisches Capability-Flag nutzen
oder – wenn es wirklich fehlt – ein präzises, standardmäßig deaktiviertes
Interface ergänzen.
3. Signal und Capability gemeinsam implementieren: Ein Capability-Flag ohne
passendes Signal erzeugt eine tote Verbindung; ein Signal ohne angekündigte
Capability wird vom Handler nicht verbunden.
4. Nachrichtenstatus, Read-Receipts, Avatar- und History-Updates nur dann
anbieten, wenn der Adapter die jeweiligen Ereignisse tatsächlich zuverlässig
liefern kann. Matrix ist ein Beispiel für delivery-/receipt-spezifische Signale
in `matrix.h`; die generischen Flags stehen in `iprotocolmessaging.h`.
5. Im gemeinsamen Handler weder auf konkrete Adapterklassen casten noch
protokollspezifische Eventtypen auswerten.

Die Signaturen und Verbindungen bestehender Signale sind in
`src/plugins/chatmessagehandler/chatmessagehandler.cpp` bei `startPlugin()`
sichtbar; Matrix verdrahtet seine Netzwerkereignisse in
`src/plugins/matrix/matrix.cpp` mit Adapter-Slots und generischen Signalen.

## Implementierungsreihenfolge

1. **Identitäten festlegen.** Persistent `AccountId`, Provider-`streamId()` und
native `ConversationId` separat definieren. Die Conversation-ID muss über
Roster, Öffnen, Verlauf, eingehende Events und Versand identisch bleiben.
2. **Plugin-Grundgerüst anlegen.** CMake-Verzeichnis, Quelllisten, Qt-Metadaten
und `plugin_list.cmake` registrieren.
3. **Lebenszyklus implementieren.** `pluginInfo()`, `initConnections()`,
`initObjects()`, `initSettings()` und `startPlugin()` in die bestehende
Init-Reihenfolge einfügen. Provider per `pluginInterface(...)` auffinden und
`plugin->instance()` auf das gewünschte Interface casten;
`Qt::UniqueConnection` beim Verbinden verwenden.
4. **Messaging-Adapter bauen.** Native Events in `BasicMessage` und generische
IDs umsetzen, `protocolMessageReceived` auslösen und `sendMessage()` asynchron
über den vorhandenen Transportpfad implementieren.
5. **Nur benötigte optionale Interfaces ergänzen.** Roster, Präsenz, Typing,
Verlauf, Avatare, Benachrichtigungen oder Aktionen anhand der tatsächlich
unterstützten Protokollfunktionen anbieten. Nicht unterstützte Fähigkeiten
bleiben deaktiviert.
6. **Eigene Fehler- und Sicherheitsgrenzen beachten.** Credentials nie
protokollieren; private Nachrichteninhalte und Tokens nicht in Diagnoseausgaben
schreiben. Fehler über den vorhandenen Status-/UI-Pfad melden; neue Qt-Logs an
`--debug` binden.
7. **Verhalten zuerst testen.** Für jede neue Funktion einen fokussierten
`*_test.cpp`-Regressionstest vor der Implementierung schreiben und als
explizites Testtarget im Plugin-CMake registrieren. Bei Netzwerkpfaden lokale
deterministische Testseams statt Homeserver verwenden. Keine einmaligen
Shell-/Python-Skripte als dauerhafte Verhaltenstests.
8. **Gezielt bauen und prüfen.** Erst das neue Plugin-Target und die
fokussierten Tests bauen, dann relevante Integrationstargets.
Konfigurationserfolg, Build, Test und Live-/Homeserver-Verhalten getrennt
ausweisen.

## Test-CMake-Beispiel

Die Projekt-CMake-Dateien deklarieren Testtargets häufig innerhalb von `if
(BUILD_TESTING)` und `EXCLUDE_FROM_ALL`, damit sie bei Bedarf ausdrücklich
gebaut werden:

```cmake
if (BUILD_TESTING)
    add_executable(examplechat_protocol_tests EXCLUDE_FROM_ALL
        examplechat_protocol_test.cpp examplechat_protocol.cpp)
    target_include_directories(examplechat_protocol_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(examplechat_protocol_tests PRIVATE cxx_std_17)
    target_link_libraries(examplechat_protocol_tests PRIVATE
        Qt6::Core Qt6::Network)
endif()
```

Die Linkbibliotheken sind nur ein Beispiel: tatsächliche Qt-Module und
Adapterabhängigkeiten aus den benutzten Includes und dem bestehenden
Plugin-CMake ableiten. Ein erfolgreiches Build-Target allein ist kein
Verhaltenstest; das erzeugte Testprogramm explizit starten.

## Build im Vacuum-Checkout

Für diesen Checkout gelten zusätzlich `/data/AGENTS.md` und die
Nix-Toolchain-Regeln:

```sh
cmake -S /data -B /data/build-nix -DBUILD_TESTING=ON
cmake --build /data/build-nix --target examplechat -j2
cmake --build /data/build-nix --target examplechat_protocol_tests -j2
/data/build-nix/src/plugins/examplechat/examplechat_protocol_tests
```

Nur `/data/build-nix` als Build-Verzeichnis verwenden; `/data/build` ist nicht
zulässig. Bei CMake-Änderungen zuerst neu konfigurieren. Falls der
Nix-GCC-Wrapper den glibc-Includepfad falsch einordnet, ausschließlich in der
Prozessumgebung des Build-Aufrufs den betroffenen `-isystem`-Eintrag wie in
`AGENTS.md` auf `-idirafter` umstellen. Keine Nix-Pfade oder
Compiler-Workarounds in CMake-Dateien eintragen.

## Prüfliste vor Abschluss

- [ ] Plugin ist in `plugin_list.cmake` registriert und das
Shared-Library-Target wird erzeugt.
- [ ] `Q_PLUGIN_METADATA` und `Q_INTERFACES` stimmen mit der implementierten
Plugin-/Interfaceklasse überein.
- [ ] Account-, Stream- und Gesprächsidentitäten werden nicht vermischt.
- [ ] Der gemeinsame Chat-Handler erhält generische `BasicMessage`-Ereignisse
und kann über den bestehenden Messaging-Adapter senden.
- [ ] Capability-Flags spiegeln tatsächliche Unterstützung wider; unsupported
Defaults bleiben sicher deaktiviert.
- [ ] Adapterdetails und Protokolltypen sind auf das jeweilige Plugin
beschränkt.
- [ ] Fokussierte C++-Regressionstests wurden gebaut und explizit ausgeführt.
- [ ] Plugin-Build, Tests und Live-Protokollprüfung sind getrennt dokumentiert.

