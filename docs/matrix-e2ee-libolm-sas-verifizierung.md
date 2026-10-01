# Matrix E2EE mit libolm -- Ablauf, Geräteverifizierung, Emoji-SAS und Beispielcode

> **Hinweis (Stand 2026):** `libolm` ist inzwischen deprecated. Für neue
> Implementierungen empfiehlt das Matrix-Projekt **vodozemac** (Rust).
> Dieses Dokument beschreibt trotzdem bewusst den Ablauf mit **libolm**,
> weil die zugrunde liegenden Matrix-Konzepte (Olm, Megolm,
> SAS-Verifikation) weiterhin wichtig sind.

------------------------------------------------------------------------

# 1. Überblick

Matrix E2EE besteht vereinfacht aus mehreren Ebenen:

``` text
Matrix E2EE
│
├── Geräteidentität
│   ├── Curve25519 Identity Key
│   └── Ed25519 Signing Key
│
├── Olm
│   └── 1:1 verschlüsselter Kanal zwischen Geräten
│
├── Megolm
│   └── effiziente Verschlüsselung von Raum-Nachrichten
│
└── Geräteverifizierung
    └── SAS (Short Authentication String)
        ├── Emoji
        └── Dezimalzahlen
```

Wichtig ist die Unterscheidung:

``` text
Olm
    Gerät A  <──────────────>  Gerät B

Megolm
    Gerät A  ───────────────>  viele Geräte im Raum
```

Olm wird unter anderem verwendet, um Megolm-Schlüssel sicher zwischen
Geräten auszutauschen.

------------------------------------------------------------------------

# 2. Geräteidentität

Jede Matrix-Installation bzw. jedes Matrix-Gerät besitzt eigene
kryptografische Schlüssel.

Beispielsweise:

``` text
User:
@alice:example.org

Device:
ALICEPHONE

Identity Keys:
├── curve25519:ALICEPHONE
└── ed25519:ALICEPHONE
```

Ein Benutzer kann mehrere Geräte besitzen:

``` text
@alice:example.org
│
├── ALICEPHONE
│   ├── Curve25519 Key
│   └── Ed25519 Key
│
├── ALICELAPTOP
│   ├── Curve25519 Key
│   └── Ed25519 Key
│
└── ALICETABLET
    ├── Curve25519 Key
    └── Ed25519 Key
```

E2EE arbeitet daher nicht nur mit User IDs, sondern wesentlich mit
**Devices**.

------------------------------------------------------------------------

# 3. libolm Account

Mit libolm wird pro Matrix-Gerät ein Olm-Account erzeugt.

Pseudocode:

``` cpp
OlmAccount* account = olm_account(...);

olm_create_account(
    account,
    randomBytes,
    randomLength
);
```

Der Account enthält unter anderem:

``` text
Identity Keys
One-Time Keys
Fallback Keys
```

Die Identity Keys können anschließend ausgelesen werden.

Konzeptionell:

``` cpp
olm_account_identity_keys(
    account,
    output,
    outputLength
);
```

Das Ergebnis enthält typischerweise:

``` json
{
  "curve25519": "<curve25519-public-key>",
  "ed25519": "<ed25519-public-key>"
}
```

------------------------------------------------------------------------

# 4. Device Keys auf den Homeserver hochladen

Der Client veröffentlicht die öffentlichen Device Keys über Matrix.

Konzeptionell:

``` http
POST /_matrix/client/v3/keys/upload
```

Ein vereinfachter Device-Key-Datensatz:

``` json
{
  "device_keys": {
    "user_id": "@alice:example.org",
    "device_id": "ALICEPHONE",
    "algorithms": [
      "m.olm.v1.curve25519-aes-sha2",
      "m.megolm.v1.aes-sha2"
    ],
    "keys": {
      "curve25519:ALICEPHONE": "<curve25519-key>",
      "ed25519:ALICEPHONE": "<ed25519-key>"
    },
    "signatures": {
      "@alice:example.org": {
        "ed25519:ALICEPHONE": "<signature>"
      }
    }
  }
}
```

Der Ed25519-Schlüssel dient unter anderem zur Signaturprüfung.

------------------------------------------------------------------------

# 5. One-Time Keys

Damit ein anderes Gerät eine neue Olm-Session zu diesem Gerät aufbauen
kann, werden One-Time Keys erzeugt.

Beispiel:

``` cpp
olm_account_generate_one_time_keys(
    account,
    numberOfKeys,
    randomBytes,
    randomLength
);
```

Danach werden die öffentlichen One-Time Keys ausgelesen und zum
Homeserver hochgeladen.

Konzeptionell:

``` text
Alice Phone
│
├── Identity Key
│
└── One-Time Keys
    ├── OTK 1
    ├── OTK 2
    ├── OTK 3
    └── ...
```

Andere Geräte können einen solchen Key über Matrix anfordern.

------------------------------------------------------------------------

# 6. Olm-Session zwischen zwei Geräten

Angenommen:

``` text
Alice Phone
    │
    │
    ▼
Bob Laptop
```

Alice benötigt:

``` text
Bob Curve25519 Identity Key
+
Bob One-Time Key
```

Dann erzeugt Alice eine Outbound-Olm-Session.

Konzeptionell mit libolm:

``` cpp
OlmSession* session = olm_session(...);

olm_create_outbound_session(
    session,
    aliceAccount,

    bobIdentityKey,
    bobIdentityKeyLength,

    bobOneTimeKey,
    bobOneTimeKeyLength,

    randomBytes,
    randomLength
);
```

Danach kann Alice eine verschlüsselte Olm-Nachricht erzeugen.

``` text
Alice
  │
  │ Olm ciphertext
  ▼
Homeserver
  │
  │ ciphertext
  ▼
Bob
```

Der Homeserver sieht den Klartext nicht.

------------------------------------------------------------------------

# 7. Empfang einer neuen Olm-Session

Bob erhält zunächst eine sogenannte Pre-Key-Nachricht.

Er erzeugt daraus eine Inbound-Session.

Konzeptionell:

``` cpp
OlmSession* session = olm_session(...);

olm_create_inbound_session(
    session,
    bobAccount,
    preKeyMessage,
    preKeyMessageLength
);
```

Danach kann Bob die Nachricht entschlüsseln.

Anschließend sollte der verwendete One-Time Key aus dem Account entfernt
werden.

Konzeptionell:

``` cpp
olm_remove_one_time_keys(
    bobAccount,
    session
);
```

Damit existiert nun auf beiden Seiten eine Olm-Session:

``` text
Alice Device                     Bob Device

Olm Session A  <──────────────>  Olm Session B
```

------------------------------------------------------------------------

# 8. Warum Matrix zusätzlich Megolm verwendet

Olm ist für direkte Geräte-zu-Geräte-Kommunikation geeignet.

In einem Raum mit vielen Teilnehmern wäre es aber teuer, jede
Chatnachricht separat für jedes Gerät mit Olm zu verschlüsseln.

Beispiel:

``` text
Raum mit 100 Geräten

eine Nachricht
      │
      ├── Olm für Gerät 1
      ├── Olm für Gerät 2
      ├── Olm für Gerät 3
      ├── ...
      └── Olm für Gerät 100
```

Matrix verwendet deshalb für Raum-Nachrichten **Megolm**.

------------------------------------------------------------------------

# 9. Megolm Outbound Group Session

Ein sendendes Gerät erzeugt eine Outbound-Megolm-Session.

Konzeptionell:

``` cpp
OlmOutboundGroupSession* groupSession =
    olm_outbound_group_session(...);

olm_init_outbound_group_session(
    groupSession,
    randomBytes,
    randomLength
);
```

Diese Session besitzt unter anderem:

``` text
session_id
session_key
message_index
```

Der `session_key` muss anschließend an die berechtigten Empfängergeräte
verteilt werden.

------------------------------------------------------------------------

# 10. Megolm-Schlüssel über Olm verteilen

Der Megolm Session Key wird **nicht einfach öffentlich in den Raum
gestellt**.

Stattdessen wird er über die bereits aufgebauten Olm-Kanäle an die
einzelnen Geräte geschickt.

Typischer Inhalt:

``` json
{
  "type": "m.room_key",
  "content": {
    "algorithm": "m.megolm.v1.aes-sha2",
    "room_id": "!room:example.org",
    "session_id": "<session-id>",
    "session_key": "<session-key>"
  }
}
```

Konzeptionell:

``` text
                  Megolm Session Key
                         │
             ┌───────────┼───────────┐
             │           │           │
          via Olm     via Olm     via Olm
             │           │           │
             ▼           ▼           ▼
         Bob Phone   Bob Laptop   Carol Phone
```

Danach besitzen alle berechtigten Geräte die benötigte Megolm Inbound
Group Session.

------------------------------------------------------------------------

# 11. Verschlüsselte Raum-Nachricht

Die eigentliche Chatnachricht:

``` json
{
  "type": "m.room.message",
  "content": {
    "msgtype": "m.text",
    "body": "Hallo Bob!"
  }
}
```

wird mit der Outbound-Megolm-Session verschlüsselt.

Konzeptionell:

``` cpp
olm_group_encrypt(
    groupSession,
    plaintext,
    plaintextLength,
    ciphertext,
    ciphertextLength
);
```

Über Matrix wird anschließend ein Event wie dieses übertragen:

``` json
{
  "type": "m.room.encrypted",
  "content": {
    "algorithm": "m.megolm.v1.aes-sha2",
    "sender_key": "<curve25519-key>",
    "session_id": "<session-id>",
    "ciphertext": "<ciphertext>"
  }
}
```

Der Homeserver speichert nur das verschlüsselte Event.

------------------------------------------------------------------------

# 12. Megolm-Nachricht entschlüsseln

Der Empfänger sucht anhand von:

``` text
room_id
sender_key
session_id
```

die passende Inbound Group Session.

Dann wird entschlüsselt.

Konzeptionell:

``` cpp
olm_group_decrypt(
    inboundGroupSession,
    ciphertext,
    ciphertextLength,

    plaintext,
    plaintextLength,

    &messageIndex
);
```

Danach erhält der Client wieder den ursprünglichen Inhalt:

``` json
{
  "type": "m.room.message",
  "content": {
    "msgtype": "m.text",
    "body": "Hallo Bob!"
  }
}
```

------------------------------------------------------------------------

# 13. Warum Device Verification notwendig ist

Nur weil ein Homeserver behauptet:

``` text
Device ALICEPHONE
hat den Schlüssel
curve25519:xyz
```

weiß Bob noch nicht kryptografisch sicher, dass dieser Schlüssel
tatsächlich zu Alices echtem Gerät gehört.

Dafür gibt es die Geräteverifizierung.

Eine verbreitete Methode ist:

``` text
SAS
Short Authentication String
```

Dabei vergleichen beide Benutzer eine kurze Darstellung eines gemeinsam
abgeleiteten Geheimnisses.

Matrix unterstützt unter anderem:

``` text
Emoji
oder
Dezimalzahlen
```

------------------------------------------------------------------------

# 14. SAS-Verifikation -- Gesamtfluss

Der moderne Ablauf sieht ungefähr so aus:

``` text
Alice                           Bob

  │                              │
  │ m.key.verification.request   │
  ├─────────────────────────────►│
  │                              │
  │ m.key.verification.ready     │
  │◄─────────────────────────────┤
  │                              │
  │ m.key.verification.start     │
  ├─────────────────────────────►│
  │                              │
  │ m.key.verification.accept    │
  │◄─────────────────────────────┤
  │                              │
  │ m.key.verification.key       │
  ├─────────────────────────────►│
  │                              │
  │ m.key.verification.key       │
  │◄─────────────────────────────┤
  │                              │
  │      Emoji vergleichen       │
  │                              │
  │ m.key.verification.mac       │
  ├─────────────────────────────►│
  │                              │
  │ m.key.verification.mac       │
  │◄─────────────────────────────┤
  │                              │
  │ m.key.verification.done      │
  ├─────────────────────────────►│
  │                              │
  │ m.key.verification.done      │
  │◄─────────────────────────────┤
```

------------------------------------------------------------------------

# 15. `m.key.verification.request`

Alice startet die Verifikation.

Beispiel:

``` json
{
  "methods": [
    "m.sas.v1"
  ],
  "timestamp": 1760000000000
}
```

Die Nachricht enthält außerdem abhängig vom Transport weitere Felder
beziehungsweise die Transaktionszuordnung.

Der Empfänger sollte unter anderem prüfen:

``` text
Ist die Transaktion bekannt?
Ist die Anfrage zeitlich plausibel?
Ist m.sas.v1 unterstützt?
```

------------------------------------------------------------------------

# 16. `m.key.verification.ready`

Bob akzeptiert die Anfrage und teilt mit, welche Verfahren er
unterstützt.

``` json
{
  "methods": [
    "m.sas.v1"
  ]
}
```

Nun wissen beide Geräte:

``` text
Alice unterstützt m.sas.v1
Bob unterstützt m.sas.v1

→ SAS kann verwendet werden
```

------------------------------------------------------------------------

# 17. `m.key.verification.start`

Alice startet SAS.

Beispiel:

``` json
{
  "method": "m.sas.v1",

  "key_agreement_protocols": [
    "curve25519-hkdf-sha256"
  ],

  "hashes": [
    "sha256"
  ],

  "message_authentication_codes": [
    "hkdf-hmac-sha256"
  ],

  "short_authentication_string": [
    "emoji",
    "decimal"
  ]
}
```

Dieses Objekt ist wichtig, weil Bob daraus später einen **Commitment
Hash** berechnet.

------------------------------------------------------------------------

# 18. SAS-Objekt mit libolm erzeugen

Bob erzeugt ein SAS-Objekt.

Je nach libolm-Binding sieht der Code unterschiedlich aus.

JavaScript-artiges Beispiel:

``` javascript
const sas = new Olm.SAS();

const myPublicKey = sas.get_pubkey();
```

Das SAS-Objekt besitzt ein temporäres Curve25519-Schlüsselpaar.

Wichtig:

``` text
SAS Key
≠
Device Identity Key
```

Der SAS-Key ist nur für diese Verifikation bestimmt.

------------------------------------------------------------------------

# 19. Commitment erzeugen

Bob muss verhindern, dass sein temporärer Schlüssel nach Kenntnis von
Alices Schlüssel manipulativ ausgewählt wird.

Dafür wird ein Commitment verwendet.

Konzeptionell:

``` text
commitment =
SHA256(
    bobEphemeralPublicKey
    +
    canonicalJson(startEvent)
)
```

Mit libolm-artigem JavaScript:

``` javascript
const sas = new Olm.SAS();

const canonicalStart = canonicalJson(startContent);

const utility = new Olm.Utility();

const commitment = utility.sha256(
    sas.get_pubkey() + canonicalStart
);

utility.free();
```

Der **Canonical JSON** muss exakt nach den Matrix-Regeln erzeugt werden.

------------------------------------------------------------------------

# 20. `m.key.verification.accept`

Bob sendet die gemeinsam ausgewählten Verfahren und das Commitment.

``` json
{
  "method": "m.sas.v1",

  "key_agreement_protocol":
    "curve25519-hkdf-sha256",

  "hash":
    "sha256",

  "message_authentication_code":
    "hkdf-hmac-sha256",

  "short_authentication_string": [
    "emoji",
    "decimal"
  ],

  "commitment":
    "<commitment>"
}
```

Alice speichert das Commitment.

------------------------------------------------------------------------

# 21. Temporäre Public Keys austauschen

Alice erzeugt ebenfalls ein SAS-Objekt:

``` javascript
const aliceSas = new Olm.SAS();

const alicePublicKey =
    aliceSas.get_pubkey();
```

Alice sendet:

``` json
{
  "key": "<alice-ephemeral-public-key>"
}
```

als:

``` text
m.key.verification.key
```

Bob setzt Alices Key:

``` javascript
bobSas.set_their_key(
    alicePublicKey
);
```

Bob sendet anschließend seinen eigenen temporären Public Key:

``` json
{
  "key": "<bob-ephemeral-public-key>"
}
```

Alice setzt:

``` javascript
aliceSas.set_their_key(
    bobPublicKey
);
```

------------------------------------------------------------------------

# 22. Commitment prüfen

Alice besitzt jetzt:

``` text
Bob Public Key
+
gespeichertes m.key.verification.start
+
Commitment aus accept
```

Sie berechnet erneut:

``` text
SHA256(
    bobPublicKey
    +
    canonicalJson(startEvent)
)
```

und vergleicht:

``` text
berechneter Commitment
        ==
empfangener Commitment
```

Falls nicht:

``` text
VERIFIKATION ABBRECHEN
```

Dieser Schritt darf nicht übersprungen werden.

------------------------------------------------------------------------

# 23. Gemeinsames SAS-Geheimnis

Nachdem beide Seiten den Public Key der anderen Seite gesetzt haben,
kann libolm über ECDH dasselbe gemeinsame Geheimnis ableiten.

Konzeptionell:

``` text
Alice private SAS key
        +
Bob public SAS key
        │
        ▼
   Shared Secret


Bob private SAS key
        +
Alice public SAS key
        │
        ▼
   Shared Secret
```

Bei korrekter Verbindung gilt:

``` text
Alice Shared Secret
        ==
Bob Shared Secret
```

------------------------------------------------------------------------

# 24. SAS-Info-String

Für die Ableitung der Vergleichsdaten wird zusätzlich ein definierter
Info-String verwendet.

Konzeptionell:

``` text
MATRIX_KEY_VERIFICATION_SAS|
<starter-user-id>|
<starter-device-id>|
<starter-sas-public-key>|
<receiver-user-id>|
<receiver-device-id>|
<receiver-sas-public-key>|
<transaction-id>
```

Beispielsweise:

``` text
MATRIX_KEY_VERIFICATION_SAS|
@alice:example.org|
ALICEPHONE|
<alice-sas-key>|
@bob:example.org|
BOBLAPTOP|
<bob-sas-key>|
transaction123
```

In der tatsächlichen Implementierung wird dies ohne die hier zur
Lesbarkeit eingefügten Zeilenumbrüche zusammengesetzt.

------------------------------------------------------------------------

# 25. Bytes für Emoji-SAS erzeugen

Mit libolm kann aus dem gemeinsamen Geheimnis Material abgeleitet
werden.

JavaScript-artig:

``` javascript
const bytes = sas.generate_bytes(
    sasInfo,
    6
);
```

Für Emoji-SAS werden 6 Bytes benötigt, aus denen sieben 6-Bit-Werte
gewonnen werden.

Konzeptionell:

``` text
6 Bytes
=
48 Bit

davon:
7 × 6 Bit
=
42 Bit Vergleichsinformation
```

------------------------------------------------------------------------

# 26. Emoji-Indizes berechnen

Aus den 6 Bytes werden sieben Werte zwischen `0` und `63` erzeugt.

Pseudocode:

``` javascript
const emoji = [
  (bytes[0] >> 2) & 0x3F,

  ((bytes[0] & 0x03) << 4) |
  ((bytes[1] >> 4) & 0x0F),

  ((bytes[1] & 0x0F) << 2) |
  ((bytes[2] >> 6) & 0x03),

  bytes[2] & 0x3F,

  (bytes[3] >> 2) & 0x3F,

  ((bytes[3] & 0x03) << 4) |
  ((bytes[4] >> 4) & 0x0F),

  ((bytes[4] & 0x0F) << 2) |
  ((bytes[5] >> 6) & 0x03)
];
```

Jeder Wert adressiert einen Eintrag aus der von Matrix definierten Liste
mit 64 Emoji.

Beispielsweise könnte daraus entstehen:

``` text
🐶  🌳  🚲  🎸  🍎  🚀  ❤️
```

Beide Geräte müssen **dieselbe Reihenfolge** anzeigen.

------------------------------------------------------------------------

# 27. Benutzer vergleicht die Emojis

Alice sieht beispielsweise:

``` text
🐶  🌳  🚲  🎸  🍎  🚀  ❤️
```

Bob sieht:

``` text
🐶  🌳  🚲  🎸  🍎  🚀  ❤️
```

Beide Benutzer bestätigen:

``` text
Die Emojis stimmen überein.
```

Falls sie unterschiedlich sind:

``` text
Alice:
🐶 🌳 🚲 🎸 🍎 🚀 ❤️

Bob:
🐶 🌳 🚲 🎸 🍎 🚀 🐱
```

muss die Verifikation abgebrochen werden.

Die Anwendung darf nicht automatisch behaupten, dass die Geräte
verifiziert sind, nur weil der SAS-Prozess technisch durchgeführt wurde.

Die **menschliche Bestätigung** ist der entscheidende
Authentifizierungsschritt.

------------------------------------------------------------------------

# 28. Alternative: Dezimal-SAS

Neben Emoji kann Matrix SAS auch als Zahlen darstellen.

Konzeptionell:

``` text
Alice:

1234 5678 9012


Bob:

1234 5678 9012
```

Clients, die SAS unterstützen, sollten insbesondere die in der
Spezifikation geforderte numerische Vergleichsmethode korrekt
unterstützen; Emoji ist eine benutzerfreundliche Darstellung.

------------------------------------------------------------------------

# 29. MAC über Device Keys

Nach erfolgreichem Emoji-/Zahlenvergleich müssen die eigentlichen Device
Keys kryptografisch an die SAS-Verifikation gebunden werden.

Dafür senden beide Seiten:

``` text
m.key.verification.mac
```

Die MACs werden aus dem gemeinsamen SAS-Geheimnis abgeleitet.

Konzeptionell:

``` text
Shared SAS Secret
        │
        ▼
      MAC
        │
        ▼
Device Keys authentifizieren
```

Beispielstruktur:

``` json
{
  "mac": {
    "ed25519:ALICEPHONE": "<mac>"
  },
  "keys": "<mac-over-key-ids>"
}
```

Der Empfänger prüft:

``` text
MAC korrekt?
        │
        ├── Nein → Abbruch
        │
        └── Ja
             │
             ▼
      Device Key authentifiziert
```

------------------------------------------------------------------------

# 30. `m.key.verification.done`

Wenn alle Prüfungen erfolgreich waren, senden beide Seiten:

``` text
m.key.verification.done
```

Damit ist die SAS-Verifikation abgeschlossen.

Konzeptionell:

``` text
Alice Device

Curve25519 Identity Key
Ed25519 Device Key
        │
        │ SAS + MAC bestätigt
        ▼
VERIFIED


Bob Device

Curve25519 Identity Key
Ed25519 Device Key
        │
        │ SAS + MAC bestätigt
        ▼
VERIFIED
```

------------------------------------------------------------------------

# 31. Abbruch einer Verifikation

Bei Fehlern wird:

``` text
m.key.verification.cancel
```

verwendet.

Abbruchgründe sind beispielsweise:

``` text
unbekannte Transaktion
Timeout
Benutzer lehnt ab
Commitment stimmt nicht
MAC stimmt nicht
Emoji stimmen nicht
unerwartete Nachricht
nicht unterstützte Methode
```

Sobald ein gültiges Cancel für die laufende Transaktion empfangen wurde,
muss der Verifikationsprozess beendet werden.

------------------------------------------------------------------------

# 32. Verifikations-State-Machine

Eine Implementierung sollte SAS als State Machine behandeln.

Beispielsweise:

``` text
IDLE
 │
 ▼
REQUESTED
 │
 ▼
READY
 │
 ▼
STARTED
 │
 ▼
ACCEPTED
 │
 ▼
KEYS_EXCHANGED
 │
 ▼
SAS_SHOWN
 │
 ▼
USER_CONFIRMED
 │
 ▼
MAC_EXCHANGED
 │
 ▼
VERIFIED
```

Jeder Zustand kann außerdem nach:

``` text
CANCELLED
```

wechseln.

Zum Beispiel:

``` text
KEYS_EXCHANGED
      │
      ├── Commitment OK
      │       │
      │       ▼
      │   SAS_SHOWN
      │
      └── Commitment falsch
              │
              ▼
          CANCELLED
```

------------------------------------------------------------------------

# 33. Persistenz von Device Trust

Nach erfolgreicher Verifikation sollte der Client speichern, dass der
konkrete Device Key verifiziert wurde.

Beispielsweise:

``` sql
CREATE TABLE devices (
    user_id             TEXT NOT NULL,
    device_id           TEXT NOT NULL,

    curve25519_key      TEXT,
    ed25519_key         TEXT,

    display_name        TEXT,

    trust_state         TEXT NOT NULL DEFAULT 'unverified',

    first_seen_at       INTEGER,
    last_seen_at        INTEGER,

    PRIMARY KEY (user_id, device_id)
);
```

Mögliche Zustände:

``` text
unverified
verified
blocked
```

Wichtig:

Die Vertrauensentscheidung sollte an den **konkreten kryptografischen
Schlüssel** gebunden sein.

Wenn sich der Device Key unerwartet ändert, darf der Client nicht
einfach den alten Trust übernehmen.

------------------------------------------------------------------------

# 34. E2EE-Daten in SQLite

Zusätzlich zur bisherigen Chat-Datenbank könnten Tabellen wie diese
verwendet werden:

``` text
e2ee_accounts
│
└── gepickelter libolm Account

devices
│
├── User ID
├── Device ID
├── Curve25519 Key
├── Ed25519 Key
└── Trust State

olm_sessions
│
├── Remote Device
├── Session ID
└── gepickelte Olm Session

megolm_outbound_sessions
│
├── Room ID
├── Session ID
└── gepickelte Outbound Group Session

megolm_inbound_sessions
│
├── Room ID
├── Sender Key
├── Session ID
└── gepickelte Inbound Group Session

verification_transactions
│
├── Transaction ID
├── Remote User
├── Remote Device
├── State
└── Verification Method
```

------------------------------------------------------------------------

# 35. libolm Pickling

libolm kann Accounts und Sessions serialisieren.

Konzeptionell:

``` cpp
olm_pickle_account(
    account,
    pickleKey,
    pickleKeyLength,
    output,
    outputLength
);
```

Analog existieren Pickle-Funktionen für Sessions.

Die Datenbank speichert dann beispielsweise:

``` text
olm_sessions

remote_user_id
remote_device_id
session_id
pickle
```

Wichtig:

``` text
SQLite DB
+
ungeschützter Pickle-Key
```

am selben unsicheren Ort bietet keinen sinnvollen Schutz.

Der Pickle-Key beziehungsweise die lokale Schlüsselverschlüsselung
sollte über einen geeigneten sicheren lokalen Secret Store
beziehungsweise OS-KeyStore geschützt werden.

------------------------------------------------------------------------

# 36. Empfang einer verschlüsselten Nachricht -- Gesamtfluss

``` text
/sync
  │
  ▼
m.room.encrypted
  │
  ▼
algorithm prüfen
  │
  ├── m.megolm.v1.aes-sha2
  │
  ▼
session_id lesen
sender_key lesen
room_id bestimmen
  │
  ▼
Inbound Megolm Session suchen
  │
  ├── gefunden
  │      │
  │      ▼
  │   decrypt
  │      │
  │      ▼
  │   Klartext-Event
  │      │
  │      ▼
  │   m.room.message
  │
  └── nicht gefunden
         │
         ▼
     Room Key fehlt
         │
         ▼
     Key Request /
     auf Schlüssel warten
```

------------------------------------------------------------------------

# 37. Senden einer verschlüsselten Nachricht -- Gesamtfluss

``` text
Benutzer schreibt Nachricht
        │
        ▼
m.room.message erzeugen
        │
        ▼
Raum E2EE aktiviert?
        │
        ├── Nein
        │     │
        │     ▼
        │   normal senden
        │
        └── Ja
              │
              ▼
      Outbound Megolm Session vorhanden?
              │
       ┌──────┴──────┐
       │             │
      Nein          Ja
       │             │
       ▼             │
Neue Megolm Session  │
       │             │
       ▼             │
Session Key via Olm  │
an Geräte verteilen  │
       │             │
       └──────┬──────┘
              ▼
       Nachricht mit
       Megolm verschlüsseln
              │
              ▼
       m.room.encrypted
              │
              ▼
          Homeserver
```

------------------------------------------------------------------------

# 38. Device-Verifikation -- Gesamtfluss

``` text
Alice Device
    │
    │ request
    ▼
Bob Device
    │
    │ ready
    ▼
Alice Device
    │
    │ start
    ▼
Bob Device
    │
    │ accept + commitment
    ▼
Alice Device
    │
    │ ephemeral SAS key
    ▼
Bob Device
    │
    │ ephemeral SAS key
    ▼
beide berechnen Shared Secret
    │
    ▼
beide erzeugen dieselben
Emoji / Zahlen
    │
    ▼
Benutzer vergleichen
    │
    ├── unterschiedlich
    │       │
    │       ▼
    │    CANCEL
    │
    └── identisch
            │
            ▼
         MAC über
        Device Keys
            │
            ▼
        MAC prüfen
            │
            ▼
          DONE
            │
            ▼
      Device VERIFIED
```

------------------------------------------------------------------------

# 39. Wichtige Sicherheitsregeln

Bei einer eigenen Implementierung sollten mindestens folgende Regeln
gelten:

1.  **Device Keys niemals allein aufgrund des Homeservers als
    vertrauenswürdig markieren.**
2.  **SAS-Commitment immer prüfen.**
3.  **MACs über die Device Keys immer prüfen.**
4.  **Unbekannte oder unerwartete Verification-Transaktionen ablehnen.**
5.  **Timeouts und `m.key.verification.cancel` korrekt behandeln.**
6.  **Emoji nicht selbst frei definieren -- die von Matrix definierte
    Zuordnung verwenden.**
7.  **Canonical JSON exakt implementieren.**
8.  **One-Time Keys korrekt verwalten und verbrauchte Keys entfernen.**
9.  **Megolm Session Keys nur an berechtigte Geräte verteilen.**
10. **Trust an konkrete Device Keys binden, nicht nur an `device_id`.**
11. **libolm Pickles und lokale Secrets angemessen schützen.**
12. **Bei neuen Projekten prüfen, ob vodozemac statt libolm verwendet
    werden kann.**

------------------------------------------------------------------------

# 40. Mentales Gesamtmodell

``` text
                   MATRIX E2EE
                       │
          ┌────────────┴─────────────┐
          │                          │
       Identity                   Messaging
          │                          │
          ▼                 ┌────────┴────────┐
       Devices              │                 │
          │                Olm              Megolm
          │                 │                 │
          │           Device ↔ Device       Room
          │                 │                 │
          │                 └──────┬──────────┘
          │                        │
          ▼                        ▼
   Device Verification       verschlüsselte
          │                  Chatnachrichten
          ▼
         SAS
          │
     ┌────┴────┐
     │         │
   Emoji     Decimal
     │
     ▼
User vergleicht
     │
     ▼
MAC über Device Keys
     │
     ▼
Device verified
```

------------------------------------------------------------------------

# 41. Kurzfassung

Die E2EE-Kette in Matrix lässt sich vereinfacht so zusammenfassen:

``` text
Device erzeugen
      │
      ▼
Identity Keys erzeugen
      │
      ▼
Device Keys hochladen
      │
      ▼
One-Time Keys hochladen
      │
      ▼
Olm Sessions zwischen Geräten
      │
      ▼
Megolm Session für Raum erzeugen
      │
      ▼
Megolm Session Key via Olm verteilen
      │
      ▼
Raum-Nachrichten mit Megolm verschlüsseln
      │
      ▼
Device Keys mittels SAS verifizieren
      │
      ▼
Emoji / Zahlen vergleichen
      │
      ▼
MACs prüfen
      │
      ▼
Gerät als verifiziert behandeln
```

------------------------------------------------------------------------

# Referenzen

-   Matrix Specification -- Olm & Megolm:
    https://spec.matrix.org/unstable/olm-megolm/
-   Matrix Specification -- Olm:
    https://spec.matrix.org/unstable/olm-megolm/olm/
-   Matrix Specification -- Megolm:
    https://spec.matrix.org/unstable/olm-megolm/megolm/
-   Matrix Client-Server API -- SAS Verification:
    https://spec.matrix.org/unstable/client-server-api/
-   libolm Repository: https://gitlab.matrix.org/matrix-org/olm
-   Matrix E2EE / Cross-signing implementation guide:
    https://matrix.org/docs/older/e2ee-cross-signing/

> Für produktiven Kryptografie-Code sollte die aktuelle stabile
> Matrix-Spezifikation als maßgebliche Quelle verwendet werden. Die
> Beispiele hier dienen als Architektur- und Implementierungsübersicht
> und ersetzen keine vollständige Prüfung aller Fehlerfälle und
> Sicherheitsanforderungen der Spezifikation.
