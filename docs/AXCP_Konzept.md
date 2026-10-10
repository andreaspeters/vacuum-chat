# AXCP – AX.25 Chat Protocol
## Technisches Konzept und Implementierungsspezifikation

**Version:** 0.1
**Status:** Konzept / MVP-Spezifikation
**Ziel:** Implementierbare Grundlage für einen textbasierten Chat über AX.25
Packet Radio

---

## 1. Ziel des Projekts

AXCP (AX.25 Chat Protocol) ist ein schlankes, textorientiertes
Messaging-Protokoll, das AX.25 als Funk-/Transportebene verwendet.

Das System soll sich für den Benutzer wie ein moderner, einfacher Chat
anfühlen, obwohl die Übertragung über klassische Packet-Radio-Infrastruktur
erfolgt.

Das System soll zunächst folgende Funktionen unterstützen:

- direkte Nachrichten zwischen zwei Stationen
- ACK-basierte Zustellbestätigung
- automatische Wiederholungen
- eindeutige Message-IDs
- Fragmentierung längerer Nachrichten
- einfache Chat-Kanäle
- Status-/Presence-Informationen
- später erweiterbar um Digipeater, Routing und Store-and-Forward

Wichtig: AX.25 selbst soll nicht ersetzt werden. AXCP ist eine darüberliegende
Anwendungsschicht.

---

# 2. Architektur

```text
┌───────────────────────────────────────────────┐
│                  AXCP Client                  │
│                                               │
│  GUI                                          │
│  ├── Chat                                     │
│  ├── Kontakte                                 │
│  └── Rooms                                    │
├───────────────────────────────────────────────┤
│              AXCP Protocol Layer              │
│                                               │
│  Message ID / Sequencing / ACK / Retry        │
│  Fragmentation / Reassembly / Routing         │
├───────────────────────────────────────────────┤
│                  AX.25                        │
│                                               │
│        UI-Frames / ggf. I-Frames              │
├───────────────────────────────────────────────┤
│              TNC / Modem / KISS               │
├───────────────────────────────────────────────┤
│                   Funk                        │
│        1200 / 9600 / weitere Datenraten       │
└───────────────────────────────────────────────┘
```

Die Anwendung muss die AX.25-Adresse als Transportadresse verwenden können,
soll aber nicht von einem bestimmten TNC oder einer bestimmten
Soundkartenlösung abhängig sein.

---

# 3. Designziele

## 3.1 Primäre Ziele

1. Einfach zu implementieren
2. Sehr geringe Protokoll-Overheads
3. Funktioniert auf langsamen Packet-Radio-Verbindungen
4. Robust gegen Paketverluste
5. Erkennung von Duplikaten
6. Erweiterbar
7. Plattformunabhängig
8. Trennung von Chat-Logik und AX.25-Transport
9. Für direkte Verbindungen und Relays geeignet
10. Offline-/Store-and-Forward später ermöglichen

## 3.2 Nicht-Ziele des MVP

Folgende Funktionen sollen nicht Bestandteil des ersten MVP sein:

- Voice
- Bilder
- Dateien
- Audio
- Video
- komplexes Routing
- automatische Internet-Gateways
- große Gruppen mit tausenden Teilnehmern
- komplexe Benutzerverwaltung

---

# 4. Terminologie

| Begriff | Bedeutung |
|---|---|
| Node | AXCP-fähige Station |
| Client | Programm, das ein Benutzer bedient |
| Relay | AXCP-fähige Station, die Nachrichten weiterleitet |
| Peer | direkte Kommunikationsstation |
| Message | logische Chatnachricht |
| Fragment | Teil einer Message |
| Channel | logischer Chatraum |
| ACK | Empfangsbestätigung |
| Message ID | eindeutige Kennung einer Nachricht |
| Sequence Number | laufende Nummer innerhalb einer Sitzung |
| Presence | Erreichbarkeits-/Statusinformation |

---

# 5. Transport

AXCP verwendet AX.25 als Transport.

Der MVP sollte primär mit AX.25 UI-Frames arbeiten, da diese für
Broadcast-/Datagram-artige Nachrichten geeignet sind.

Die Implementierung soll eine Transportabstraktion besitzen:

```text
Ax25Transport
├── connect()
├── disconnect()
├── send(destination, payload)
├── receive()
└── status()
```

Damit kann später neben KISS/TNC beispielsweise ein Linux AX.25 Socket oder
eine andere AX.25-Anbindung verwendet werden.

Die Chatlogik darf nicht direkt von KISS abhängig sein.

---

# 6. AXCP Paketformat

## 6.1 Grundstruktur

Der Protokoll-Payload eines AX.25-Frames soll logisch folgende Struktur
besitzen:

```text
┌────────┬────────┬────────┬────────┬────────┬──────────────┐
│ VER    │ TYPE   │ FLAGS  │ MSG-ID │ SEQ    │ PAYLOAD      │
│ 1 Byte │ 1 Byte │ 1 Byte │ 4 Byte │ 2 Byte │ 0..N Bytes   │
└────────┴────────┴────────┴────────┴────────┴──────────────┘
```

Alle Integer-Werte werden im Network Byte Order / Big Endian übertragen.

### VER

Protokollversion.

MVP:

```text
0x01
```

### TYPE

Nachrichtentyp.

Empfohlene Werte:

```text
0x01 HELLO
0x02 MSG
0x03 ACK
0x04 NACK
0x05 PING
0x06 PONG
0x07 JOIN
0x08 LEAVE
0x09 PRESENCE
0x0A INFO
```

Für spätere Versionen können weitere Werte reserviert werden.

### FLAGS

Bitfeld.

Vorschlag:

```text
Bit 0: FRAGMENTED
Bit 1: FINAL_FRAGMENT
Bit 2: RELAYED
Bit 3: ACK_REQUIRED
Bit 4: CHANNEL_MESSAGE
Bit 5-7: reserviert
```

### MSG-ID

32-Bit Message-ID.

Sie muss innerhalb eines sinnvollen Zeitraums eindeutig sein.

Empfehlung:

```text
Random / cryptographically strong 32-bit value
```

Alternativ kann sie aus Node-ID + Counter gebildet werden.

### SEQ

16-Bit Sequenznummer.

Die Sequenznummer ist pro Kommunikationspartner oder Session zu führen.

---

# 7. Payload

Der Payload soll bei normalen Nachrichten möglichst klein bleiben.

Für den MVP ist UTF-8 vorgeschrieben.

Beispiel:

```text
Hallo, bist du QRV?
```

Maximale Payload-Größe soll konfigurierbar sein.

Empfohlener Default:

```text
MAX_PAYLOAD = 180 bytes
```

Die tatsächliche Größe muss vom AX.25/TNC-Setup abhängig gemacht werden
können.

---

# 8. Nachrichtentypen

## 8.1 HELLO

Wird beim Aufbau einer logischen Session gesendet.

Beispiel:

```text
HELLO
version=1
node=DL1AAA-7
capabilities=MSG,ACK,FRAG
```

Für den eigentlichen Wire-Transport soll jedoch bevorzugt ein binäres Format
verwendet werden.

HELLO dient hauptsächlich dazu, Version und Fähigkeiten auszutauschen.

---

## 8.2 MSG

Normale Chatnachricht.

Beispiel:

```text
FROM=DL1AAA-7
TO=DL1BBB-9
MSG-ID=0xA31F92C1
SEQ=42
TEXT=Hallo!
```

ACK ist standardmäßig erforderlich.

---

## 8.3 ACK

Bestätigt den Empfang einer Message oder eines Fragments.

```text
MSG-ID=0xA31F92C1
SEQ=42
```

Optional kann ein Statusbyte ergänzt werden:

```text
0x00 = received
0x01 = duplicate
0x02 = stored
```

Wichtig:

Bei einem Duplikat muss ebenfalls ein ACK gesendet werden. Dadurch wird
verhindert, dass der Sender unnötig weiter retransmittiert.

---

## 8.4 NACK

Signalisiert einen Fehler.

Beispiele:

```text
0x01 unsupported_version
0x02 malformed_packet
0x03 invalid_fragment
0x04 message_too_large
0x05 unsupported_feature
```

---

## 8.5 PING / PONG

Für Erreichbarkeit und Diagnose.

```text
PING → PONG
```

Das System soll damit Round-Trip-Time messen können.

---

## 8.6 PRESENCE

Optionale Statusinformation.

Beispiel:

```text
ONLINE
TTL=120
```

Presence darf den Funkkanal nicht unnötig belasten.

Daher:

- nicht permanent senden
- Default-Intervall mindestens 60 Sekunden
- Presence optional deaktivierbar

---

## 8.7 JOIN / LEAVE

Logisches Betreten oder Verlassen eines Channels.

Beispiel:

```text
JOIN #technik
```

---

# 9. Message-ID und Duplikaterkennung

Jede Message muss eine eindeutige Message-ID besitzen.

Empfänger führen einen Cache bereits empfangener IDs.

Beispiel:

```text
received_ids:
    A31F92C1
    773A1B20
    9D882E10
```

Wenn eine Message erneut eintrifft:

```text
if msg_id in received_ids:
    send ACK
    discard duplicate
else:
    store
    display
    send ACK
```

Der Cache muss begrenzt sein.

Empfehlung:

```text
max_entries = 1000
```

und zusätzlich:

```text
expire_after = 24h
```

Die Werte sollen konfigurierbar sein.

---

# 10. ACK- und Retry-Verhalten

## 10.1 Ablauf

```text
A                                  B

MSG #123 ------------------------>

        <------------------------ ACK #123

Message delivered
```

Wenn kein ACK kommt:

```text
MSG #123
    │
    ├── timeout
    │
    ├── retry #1
    │
    ├── timeout
    │
    ├── retry #2
    │
    └── retry #3
```

Default:

```text
MAX_RETRIES = 3
```

Timeout:

```text
ACK_TIMEOUT = 5 Sekunden
```

Diese Werte müssen konfigurierbar sein.

Bei langen oder mehrstufigen Funkstrecken soll der Timeout später dynamisch an
RTT und Relay-Anzahl angepasst werden können.

---

# 11. Fragmentierung

AX.25-Payloads sind für längere Chatnachrichten begrenzt.

Eine logische Message muss deshalb fragmentiert werden können.

Beispiel:

```text
MSG-ID = A31F92C1

Fragment 1/3
Fragment 2/3
Fragment 3/3
```

Dafür muss der Header um Fragmentinformationen erweitert werden.

Empfehlung:

```text
FRAGMENT_ID     1 Byte
FRAGMENT_COUNT  1 Byte
```

Beispiel:

```text
┌──────────────────────────────────────┐
│ Header                               │
├──────────────────────────────────────┤
│ MSG-ID                               │
│ Fragment ID = 0                      │
│ Fragment Count = 3                   │
├──────────────────────────────────────┤
│ Payload                              │
└──────────────────────────────────────┘
```

Der Empfänger speichert Fragmente temporär.

Nach Empfang aller Fragmente:

```text
fragment 0
fragment 1
fragment 2
      ↓
reassemble()
      ↓
complete message
```

Fehlende Fragmente müssen retransmittiert werden können.

---

# 12. Channels

Channels sind logische Räume.

Beispiele:

```text
#allgemein
#technik
#shack
#lokal
```

Für den MVP kann ein Channel einfach als Textkennung übertragen werden.

Beispiel:

```text
channel="technik"
```

Channel-Namen:

- UTF-8
- maximal 32 Bytes
- nur definierte Zeichen zulassen
- empfohlen: `[a-z0-9_-]+`

---

# 13. Private Nachrichten

CLI:

```text
/msg DL1BBB-9 Hallo, bist du QRV?
```

Protokoll:

```text
FROM=local
TO=DL1BBB-9
TYPE=MSG
MSG-ID=A31F92C1
```

Die Anwendung muss die AX.25-Adresse des Zielsystems eindeutig bestimmen
können.

---

# 14. Store-and-Forward

Store-and-Forward ist Bestandteil der geplanten Erweiterung.

Ein Relay kann eine Nachricht temporär speichern:

```text
DL1AAA
   │
   ▼
DB0ABC
   │
   │ stores message
   │
   ▼
DL1BBB kommt später online
```

Dann:

```text
DL1BBB → DB0ABC: HELLO

DB0ABC → DL1BBB:
    pending messages = 3
```

Danach werden die Nachrichten übertragen.

Eine gespeicherte Nachricht soll nach erfolgreicher Zustellung gelöscht oder
als delivered markiert werden.

Konfigurierbare Parameter:

```text
STORE_MAX_MESSAGES
STORE_MAX_AGE
STORE_MAX_BYTES
```

---

# 15. Relay

Ein Relay soll Nachrichten weiterleiten können.

Beispiel:

```text
DL1AAA
   │
   ▼
DB0ABC
   │
   ▼
DB0XYZ
   │
   ▼
DL1BBB
```

Relay-Nachrichten bekommen:

```text
RELAYED = 1
```

Zusätzlich sollte eine Hop-Limit-/TTL-Funktion implementiert werden.

Beispiel:

```text
TTL = 5
```

Bei jedem Relay:

```text
TTL--
```

Bei:

```text
TTL == 0
```

wird die Nachricht verworfen.

Damit werden Routing-Schleifen verhindert.

---

# 16. Routing

Routing ist zunächst bewusst einfach zu halten.

MVP:

- direkte Verbindung
- optional statische Next-Hop-Konfiguration

Beispiel:

```yaml
routes:
  DL1BBB-9: DB0ABC-0
  DL1CCC-7: DB0XYZ-0
```

Ein späteres Protokoll kann dynamische Routen lernen.

Für V1 soll kein komplexes Mesh-Routing implementiert werden.

---

# 17. Fehlerbehandlung

Folgende Fehler müssen sauber behandelt werden:

- ungültiger Header
- unbekannte Protokollversion
- unbekannter Nachrichtentyp
- ungültige UTF-8-Daten
- ungültige Fragmentnummer
- fehlendes Fragment
- doppelte Message
- ACK-Timeout
- maximale Retry-Anzahl überschritten
- TTL abgelaufen
- unbekannter Empfänger
- überfüllter Store
- Transport nicht verfügbar

Der Benutzer soll beispielsweise sehen:

```text
⚠ Message A31F92C1 konnte nicht zugestellt werden
Grund: ACK timeout
Retries: 3
```

---

# 18. Konfiguration

Beispiel:

```yaml
station:
  callsign: DL1AAA-7

transport:
  type: kiss
  device: /dev/ttyUSB0
  baudrate: 115200

protocol:
  version: 1
  max_payload: 180
  ack_timeout: 5
  max_retries: 3
  message_cache_size: 1000
  message_cache_ttl: 86400

presence:
  enabled: true
  interval: 60

relay:
  enabled: false
  ttl: 5

store_forward:
  enabled: false
  max_messages: 100
  max_age: 604800
```

---

# 19. Mock-Transport

Vor der Implementierung auf echter Funkhardware muss ein Fake-/Mock-Transport
existieren.

Beispiel:

```text
Client A
   │
   │ MockTransport
   ▼
Virtual Channel
   │
   ▼
Client B
```

Damit können:

- ACK
- Retry
- Fragmentierung
- Duplikate
- Paketverluste
- Delay
- Relay
- Routing

ohne Funkhardware getestet werden.

Der Mock-Transport soll gezielt Paketverlust simulieren können:

```text
loss_rate = 0.20
delay = 1.5s
duplicate_rate = 0.05
```

---

# 20. Tests

Es müssen automatisierte Tests vorhanden sein.

## Unit Tests

Mindestens:

```text
test_encode_packet
test_decode_packet
test_invalid_packet
test_message_id
test_duplicate_detection
test_ack
test_retry
test_fragmentation
test_reassembly
test_ttl
test_utf8
```

## Integration Tests

```text
Client A → Client B
Client A → Relay → Client B
Client A → Relay mit Paketverlust
Client A → B mit Fragmentierung
Client A → B mit Duplikaten
```

---

# 21. MVP-Abnahmekriterien

Der MVP gilt als erfolgreich, wenn folgende Szenarien funktionieren.

### Szenario 1 – Direktnachricht

```text
A → B: Hallo
B → A: ACK
```

A zeigt:

```text
✓ delivered
```

### Szenario 2 – Paketverlust

```text
A → B: Hallo
       X
A → B: Hallo
B → A: ACK
```

Die Nachricht darf beim Benutzer nur einmal erscheinen.

### Szenario 3 – Duplikat

B erhält dieselbe Message-ID zweimal.

Ergebnis:

```text
Nachricht wird einmal angezeigt.
ACK wird mindestens einmal gesendet.
```

### Szenario 4 – Fragmentierung

Eine Nachricht mit beispielsweise 500 Bytes wird in mehrere Fragmente geteilt.

B rekonstruiert exakt den ursprünglichen Text.

### Szenario 5 – Offline

B ist nicht erreichbar.

A:

```text
⚠ delivery failed
```

Es dürfen maximal `MAX_RETRIES` Übertragungsversuche stattfinden.

---

# 22. Erweiterungsstufen

## V0.1 – MVP

- AX.25 Transport
- UI-Frames
- Direktnachrichten
- Message-ID
- ACK
- Retry
- Fragmentierung
- CLI
- SQLite
- Mock-Transport

## V0.2

- Channels
- Presence
- PING/PONG
- bessere Statusanzeige

## V0.3

- Relay
- TTL
- statisches Routing
- Store-and-Forward

## V0.4

- dynamisches Routing
- mehrere Relays
- bessere Offline-Unterstützung

## V1.0

- stabiles Protokoll
- Protokollspezifikation
- Referenzimplementierung
- interoperable Clients
- ausführliche Test-Suite

---

# 23. Sicherheits- und Rechtsaspekte

Das System soll Authentizität und Integrität später optional unterstützen
können.

Dafür kann das Protokoll eine optionale Signatur vorsehen:

```text
PAYLOAD
+
SIGNATURE
```

Eine Verschlüsselungsfunktion ist nicht Bestandteil des MVP.

Bei tatsächlichem Einsatz im Amateurfunk müssen insbesondere die für den
jeweiligen Funkdienst geltenden gesetzlichen und betrieblichen Vorgaben geprüft
werden. Die Software darf nicht davon ausgehen, dass jede aus dem Internet
bekannte Verschlüsselungs- oder Authentifizierungsfunktion im jeweiligen
Amateurfunkbetrieb zulässig ist.

---

# 24. Protokoll-Erweiterbarkeit

Nicht verwendete TYPE-Werte müssen reserviert werden.

Unbekannte Message Types sollen von einem Client verworfen werden, ohne dass die
Anwendung abstürzt.

Beispiel:

```text
TYPE 0x01–0x0A
standard

TYPE 0x0B–0x7F
future standard extensions

TYPE 0x80–0xFF
experimental/private
```

Die Implementierung soll Versionskompatibilität berücksichtigen.

---

# 25. Grundprinzipien für die Implementierung

Der Entwickler soll insbesondere folgende Regeln beachten:

1. Funkbandbreite ist knapp.
2. Pakete können verloren gehen.
3. Pakete können doppelt eintreffen.
4. Pakete können verspätet eintreffen.
5. Ein Sender darf niemals davon ausgehen, dass ein gesendetes Paket angekommen
ist.
6. Jede zustellrelevante Nachricht benötigt eine eindeutige ID.
7. ACKs müssen Duplikate ebenfalls bestätigen.
8. Nachrichten dürfen nicht unbegrenzt retransmittiert werden.
9. Routing darf keine Endlosschleifen erzeugen.
10. Chat-Logik und AX.25-Transport müssen getrennt bleiben.
11. Das Protokoll muss mit kleinen Payloads effizient funktionieren.
12. Alle zeit- und größenabhängigen Parameter müssen konfigurierbar sein.

---

# 26. Beispiel eines vollständigen Ablaufs

Station A möchte B eine Nachricht schicken:

```text
User:
> /msg DL1BBB-9 Hallo B!
```

Client A:

```text
create message
    ↓
generate MSG-ID
    ↓
encode AXCP packet
    ↓
send via AX.25
```

Funk:

```text
DL1AAA-7 → DL1BBB-9
```

Station B:

```text
receive
    ↓
validate
    ↓
check MSG-ID
    ↓
store message
    ↓
display message
    ↓
send ACK
```

B sieht:

```text
[14:31] DL1AAA-7: Hallo B!
```

A empfängt:

```text
ACK A31F92C1
```

und zeigt:

```text
✓ delivered
```

---

# 27. Empfohlene Entwicklungsreihenfolge

### Phase 1

Nur Software:

```text
Packet Encoder
Packet Decoder
Mock Transport
```

### Phase 2

Messaging:

```text
MSG
ACK
Retry
Duplicate Detection
```

### Phase 3

Benutzeroberfläche:

```text
CLI
SQLite
History
Contacts
```

### Phase 4

Funk:

```text
KISS
AX.25
echte TNC
```

### Phase 5

Erweiterungen:

```text
Channels
Presence
Relay
Store-and-Forward
Routing
```

Diese Reihenfolge verhindert, dass Fehler in der Funk-/TNC-Schicht gleichzeitig
mit Fehlern im Chat-Protokoll debuggt werden müssen.

---

# 28. Ergebnis

Das Ziel ist ein System, das sich für den Benutzer ungefähr so anfühlt:

```text
$ axchat

Station: DL1AAA-7
AX.25: connected

#allgemein

[14:30] DL1BBB: Moin!
[14:31] DL1AAA: Hallo!
[14:32] DO2ABC: Ist jemand QRV?

axchat> /msg DL1BBB Bis später!

✓ delivered
```

Während darunter weiterhin klassische Packet-Radio-Technik verwendet wird:

```text
Chat
 ↓
AXCP
 ↓
AX.25
 ↓
KISS/TNC
 ↓
Packet Radio
```

Das Projekt soll dabei nicht versuchen, einen modernen Internet-Messenger
vollständig nachzubauen. Der Schwerpunkt liegt auf **minimalem Overhead,
Robustheit bei schlechten Funkbedingungen, einfacher Implementierung und
Erweiterbarkeit**.

---

## Anhang A – Offene Designentscheidungen

Vor einer stabilen V1-Spezifikation müssen noch folgende Punkte entschieden
werden:

1. Exakte AX.25-Frame-Art: ausschließlich UI oder zusätzlich I-Frames?
2. Exakte maximale AXCP-Payload abhängig vom verwendeten AX.25-Modus.
3. Binäres Wire-Format vs. TLV-Format.
4. Algorithmus zur Message-ID-Erzeugung.
5. Session-Modell für Sequenznummern.
6. Exakte Fragment-ACK-Strategie.
7. Relay-Protokoll.
8. Store-and-Forward-Protokoll.
9. Routing-Protokoll.
10. Signaturformat für spätere Versionen.
11. Verhalten bei gleichzeitigem Senden mehrerer Stationen.
12. Channel-Broadcast-Verfahren.
13. Priorisierung und Queue-Verhalten.
14. Kompatibilität zu bestehenden Packet-Radio-Netzen.

Diese Punkte sollten vor einer V1.0 als separate technische RFCs spezifiziert
werden.

---

# Ende der Spezifikation

