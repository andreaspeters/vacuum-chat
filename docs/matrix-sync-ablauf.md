# Matrix Sync -- Ablauf und Persistenz im Chatclient

Matrix verwendet beim klassischen Client-Server-Sync im Wesentlichen ein
**cursor-basiertes Long-Polling** über:

``` http
GET /_matrix/client/v3/sync
```

Der Client lädt zunächst einen Zustand, bekommt einen `next_batch`-Token
und verwendet diesen anschließend als `since`-Cursor.

------------------------------------------------------------------------

## 1. Initialer Sync

Nach dem Login besitzt der Client noch keinen Sync-Token:

``` http
GET /_matrix/client/v3/sync
Authorization: Bearer <access_token>
```

Vereinfacht könnte die Antwort so aussehen:

``` json
{
  "next_batch": "s12345_67890",

  "rooms": {
    "join": {
      "!abc:example.org": {
        "state": {
          "events": [
            {
              "type": "m.room.name",
              "state_key": "",
              "content": {
                "name": "Projekt Alpha"
              }
            },
            {
              "type": "m.room.member",
              "state_key": "@alice:example.org",
              "content": {
                "membership": "join",
                "displayname": "Alice"
              }
            }
          ]
        },

        "timeline": {
          "events": [
            {
              "event_id": "$event1",
              "type": "m.room.message",
              "sender": "@alice:example.org",
              "origin_server_ts": 1760000000000,
              "content": {
                "msgtype": "m.text",
                "body": "Hallo!"
              }
            }
          ],
          "prev_batch": "t98765"
        }
      }
    }
  }
}
```

Beim initialen Sync bekommt der Client nicht zwingend die komplette
Historie aller Räume, sondern einen **aktuellen Ausschnitt der Timeline
plus den dafür benötigten Room State**.

Für eine SQLite-Architektur ergibt sich ungefähr:

``` text
/sync
  │
  ├── rooms.join
  │      │
  │      ├── state.events
  │      │      ├──► events
  │      │      ├──► rooms
  │      │      └──► room_members
  │      │
  │      └── timeline.events
  │             ├──► events
  │             └──► messages
  │
  └── next_batch
         │
         └──► lokal speichern
```

------------------------------------------------------------------------

## 2. `next_batch` als Cursor

Angenommen, der Server antwortet mit:

``` json
{
  "next_batch": "s12345_67890"
}
```

Dann speichert der Client diesen Wert lokal.

Beim nächsten Sync:

``` http
GET /_matrix/client/v3/sync?since=s12345_67890
```

Das bedeutet sinngemäß:

> Gib mir die Änderungen seit meinem letzten bekannten Sync-Punkt.

Der Server liefert daraufhin einen **inkrementellen Sync**.

Der zurückgegebene neue `next_batch` wird wiederum für den nächsten
Aufruf verwendet.

``` text
erster Sync
    │
    ▼
next_batch = A
    │
    ▼
/sync?since=A
    │
    ▼
Änderungen
next_batch = B
    │
    ▼
/sync?since=B
    │
    ▼
Änderungen
next_batch = C
    │
    ▼
...
```

Der Token sollte als **opaque** behandelt werden. Der Client muss seine
interne Struktur nicht verstehen.

------------------------------------------------------------------------

## 3. Long-Polling

Normalerweise fragt der Client nicht in festen kurzen Intervallen erneut
ab.

Stattdessen verwendet er beispielsweise:

``` http
GET /_matrix/client/v3/sync?since=s12345_67890&timeout=30000
```

Wenn nichts passiert, kann der Server die Verbindung bis zum Timeout
offenhalten.

Kommt vorher beispielsweise eine neue Nachricht, kann der Server sofort
antworten.

Danach startet der Client direkt den nächsten `/sync`.

``` text
Client                         Homeserver

  │                                │
  │ /sync?since=A&timeout=30000     │
  ├───────────────────────────────► │
  │                                │
  │            wartet...           │
  │                                │
  │             neue Nachricht     │
  │                                │
  │ events + next_batch=B          │
  │ ◄──────────────────────────────┤
  │                                │
  │ /sync?since=B&timeout=30000     │
  ├───────────────────────────────► │
  │                                │
  │            wartet...           │
```

Damit entsteht praktisch eine kontinuierliche Synchronisation über
normales HTTP.

------------------------------------------------------------------------

## 4. Was `/sync` liefert

Eine `/sync`-Antwort enthält deutlich mehr als nur Chatnachrichten.

Grob:

``` text
/sync
│
├── next_batch
│
├── rooms
│   ├── join
│   ├── invite
│   └── leave
│
├── presence
│
├── account_data
│
├── to_device
│
├── device_lists
│
└── device_one_time_keys_count
```

Für jeden beigetretenen Raum gibt es wiederum unter anderem:

``` text
rooms.join["!room:example.org"]
│
├── state
├── timeline
├── ephemeral
├── account_data
└── summary
```

Dabei gilt grob:

-   `timeline` enthält Timeline-Events wie Nachrichten.
-   `state` dient der Synchronisierung des Raumzustands.
-   `ephemeral` enthält flüchtige Informationen wie Typing und Receipts.
-   `account_data` enthält benutzerspezifische Daten.

------------------------------------------------------------------------

## 5. `state` und `timeline`

`state` und `timeline` sind unterschiedliche Konzepte.

Beispielsweise:

``` text
state
├── m.room.name
├── m.room.avatar
├── m.room.member
├── m.room.power_levels
└── ...

timeline
├── m.room.message
├── m.room.message
├── m.reaction
├── m.room.member
├── m.room.message
└── ...
```

Wichtig:

**State Events können ebenfalls in der Timeline vorkommen.**

Beispielsweise kann Alice während eines Gesprächs ihren Display Name
ändern.

``` text
State am Anfang:
Alice = "Alice"

        │
        ▼

Timeline:

$1 m.room.message
   Alice: "Hallo"

$2 m.room.member
   Alice → "Alice Müller"

$3 m.room.message
   Alice Müller: "Mein Name wurde geändert"

        │
        ▼

State am Ende:
Alice = "Alice Müller"
```

Das ist ein wichtiger Grund, weshalb die ursprünglichen Events in der
lokalen Datenbank erhalten bleiben sollten.

------------------------------------------------------------------------

## 6. Alte Nachrichten und `prev_batch`

`next_batch` dient zum **Vorwärts-Synchronisieren**:

``` text
bekannter Stand ───────────────► neue Events
                  next_batch
```

Für ältere Nachrichten einer Raum-Timeline gibt es dagegen `prev_batch`.

Beispielsweise:

``` json
{
  "timeline": {
    "events": [
      { "event_id": "$100" },
      { "event_id": "$101" },
      { "event_id": "$102" }
    ],
    "prev_batch": "t123456"
  }
}
```

Mit diesem Token können ältere Nachrichten über:

``` http
GET /_matrix/client/v3/rooms/{roomId}/messages
```

nachgeladen werden.

Konzeptionell:

``` text
ältere Historie                  Sync

$95 $96 $97 $98 $99 | $100 $101 $102
                     ▲
                     │
                 prev_batch

                          $103 $104 ...
                              ───────►
                              next_batch
```

Das passt beispielsweise zu einer `rooms`-Tabelle mit:

``` text
rooms
│
├── room_id
├── ...
└── prev_batch
```

------------------------------------------------------------------------

## 7. `limited` und Timeline-Gaps

Zwischen zwei Syncs können sehr viele Events eintreffen.

Der Server muss nicht zwangsläufig alle davon in einer normalen
Sync-Timeline liefern.

Eine Timeline kann beispielsweise melden:

``` json
{
  "timeline": {
    "limited": true,
    "prev_batch": "gap-token",
    "events": [
      { "event_id": "$500" },
      { "event_id": "$501" }
    ]
  }
}
```

`limited: true` bedeutet, dass zwischen dem bisherigen lokalen Wissen
und dieser Timeline eine **Lücke existieren kann**.

Diese Lücke kann über `/rooms/{roomId}/messages` und die entsprechenden
Pagination-Tokens aufgefüllt werden.

Ein Client sollte deshalb nicht einfach davon ausgehen:

``` text
höchster timestamp in DB
        +
neue Sync-Events
        =
lückenlose Historie
```

Das ist bei Matrix nicht garantiert.

Für einen robusten Client kann es sinnvoll sein, Timeline-Segmente
beziehungsweise Gaps explizit zu modellieren.

------------------------------------------------------------------------

## 8. Events deduplizieren

Dasselbe Event kann über unterschiedliche API-Wege erneut auftauchen.

Deshalb sollte anhand der `event_id` dedupliziert werden.

Eine Tabelle wie:

``` sql
CREATE TABLE events (
    event_id TEXT PRIMARY KEY,
    ...
);
```

eignet sich dafür gut.

Beim Import kann beispielsweise verwendet werden:

``` sql
INSERT OR IGNORE INTO events (...)
VALUES (...);
```

Damit kann dasselbe Event gefahrlos erneut verarbeitet werden.

------------------------------------------------------------------------

## 9. Sync-State in SQLite

Der Sync-Zustand sollte pro Account gespeichert werden.

Beispielsweise:

``` sql
CREATE TABLE sync_state (
    account_id       INTEGER PRIMARY KEY,
    next_batch       TEXT,

    FOREIGN KEY (account_id)
        REFERENCES accounts(id)
        ON DELETE CASCADE
);
```

Der gespeicherte Wert könnte beispielsweise sein:

``` text
account_id = 1
next_batch = s12345_67890
```

Beim Start des Clients wird dieser Token gelesen.

------------------------------------------------------------------------

## 10. Sync transaktional speichern

Events und `next_batch` sollten **in derselben SQLite-Transaktion**
persistiert werden.

Nicht:

``` text
next_batch speichern
       ↓
Crash
       ↓
Events noch nicht gespeichert
```

Denn beim Neustart würde der Client mit dem neuen Token weitermachen und
könnte die noch nicht persistierten Daten überspringen.

Besser:

``` text
BEGIN
 │
 ├── Events speichern
 ├── materialisierte Tabellen aktualisieren
 ├── next_batch speichern
 │
COMMIT
```

Oder als SQL-Grundstruktur:

``` sql
BEGIN TRANSACTION;

-- Events speichern
-- Rooms aktualisieren
-- Members aktualisieren
-- Messages materialisieren

UPDATE sync_state
SET next_batch = ?
WHERE account_id = ?;

COMMIT;
```

Kommt es vor dem `COMMIT` zu einem Crash, wird die gesamte Transaktion
zurückgerollt.

Der alte Sync-Token bleibt dadurch erhalten.

Beim Neustart wird derselbe Bereich erneut synchronisiert.

Durch:

``` text
event_id PRIMARY KEY
```

können bereits bekannte Events dedupliziert werden.

------------------------------------------------------------------------

## 11. Verarbeitung eines Raums

Ein Raum aus `/sync` kann ungefähr folgendermaßen verarbeitet werden:

``` text
rooms.join["!room:example.org"]
              │
              ▼
        Raum sicherstellen
              │
       ┌──────┴──────┐
       │             │
       ▼             ▼
     state        timeline
       │             │
       ▼             ▼
State Events       Events
verarbeiten        speichern
       │             │
       │             ├── m.room.message
       │             │       │
       │             │       ▼
       │             │    messages
       │             │
       │             ├── m.room.member
       │             │       │
       │             │       ▼
       │             │   room_members
       │             │
       │             └── andere Events
       │
       ▼
materialisierten
Room State
aktualisieren
```

------------------------------------------------------------------------

## 12. Verarbeitung von State Events

Beispielsweise:

``` text
m.room.name
        │
        ├──► events
        │
        └──► rooms.name
```

Analog:

``` text
m.room.topic
        → rooms.topic

m.room.avatar
        → rooms.avatar_url

m.room.member
        → room_members

m.room.encryption
        → rooms.is_encrypted

m.room.tombstone
        → rooms.replacement_room_id
```

Die `events`-Tabelle enthält dabei weiterhin das ursprüngliche Event.

Die anderen Tabellen stellen lediglich den aktuellen materialisierten
Zustand dar.

------------------------------------------------------------------------

## 13. Nachrichten verarbeiten

Ein Event:

``` json
{
  "event_id": "$abc",
  "type": "m.room.message",
  "sender": "@alice:example.org",
  "origin_server_ts": 1760000000000,
  "content": {
    "msgtype": "m.text",
    "body": "Hallo!"
  }
}
```

wird zunächst nach:

``` text
events
```

geschrieben.

Anschließend können relevante Felder nach:

``` text
messages
```

materialisiert werden.

Damit ergibt sich:

``` text
m.room.message
       │
       ├──► events
       │
       └──► messages
```

Die UI kann dann schnell auf `messages` zugreifen, während `events` als
möglichst vollständige Datenbasis erhalten bleibt.

------------------------------------------------------------------------

## 14. Gesamter Sync-Loop

Der komplette Client-Ablauf kann ungefähr so aussehen:

``` text
Client startet
     │
     ▼
sync_state.next_batch lesen
     │
     ├── NULL
     │    │
     │    └── GET /sync
     │
     └── vorhanden
          │
          └── GET /sync
                ?since=<next_batch>
                &timeout=30000
                         │
                         ▼
                   Sync Response
                         │
              ┌──────────┼───────────┐
              ▼          ▼           ▼
           rooms      account     encryption/
                       data         to-device
              │
              ▼
       pro Raum verarbeiten
              │
       ┌──────┴──────┐
       ▼             ▼
     state        timeline
       │             │
       ▼             ▼
    events         events
       │             │
       ▼             ├── m.room.message
 rooms /             │       ↓
 members              │    messages
 aktualisieren        │
                      └── State Events
                              ↓
                         Room State
                         aktualisieren
              │
              ▼
       next_batch speichern
              │
              ▼
            COMMIT
              │
              ▼
       sofort nächster /sync
              │
              └───────────────┐
                              │
                              ▼
                         wiederholen
```

------------------------------------------------------------------------

# Architekturprinzip

Der Kern der lokalen Matrix-Synchronisation ist:

``` text
/sync
   ↓
Änderungen empfangen
   ↓
SQLite-Transaktion starten
   ↓
Events speichern
   ↓
materialisierte Tabellen aktualisieren
   ↓
next_batch speichern
   ↓
COMMIT
   ↓
nächster /sync
```

Damit wird die lokale SQLite-Datenbank schrittweise zu einer lokalen
Repräsentation des für den Client relevanten Matrix-Zustands.

Besonders wichtig sind dabei vier Konzepte:

``` text
next_batch
    → vorwärts synchronisieren

prev_batch
    → ältere Timeline laden

event_id
    → Events deduplizieren

limited
    → mögliche Timeline-Lücke erkennen
```

------------------------------------------------------------------------

## Referenz

Matrix Client-Server API -- Syncing:

https://spec.matrix.org/unstable/client-server-api/#syncing
