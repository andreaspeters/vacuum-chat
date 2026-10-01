# SQLite-Persistenz für ein Matrix-artiges Chatprogramm

Für einen Matrix-artigen Chatclient bietet sich eine SQLite-Persistenz
an, bei der **Räume, Benutzer/Kontakte, Mitgliedschaften und Events**
getrennt gespeichert werden.

Chatnachrichten sollten dabei **nicht vollständig von Events getrennt**
betrachtet werden: Eine Nachricht ist schließlich ein Matrix-Event. Eine
zusätzliche `messages`-Tabelle kann jedoch die für die UI häufig
benötigten Felder materialisieren.

------------------------------------------------------------------------

## Grundmodell

``` text
accounts
   │
   ├──────────────┐
   │              │
   ▼              ▼
rooms         contacts
   │              │
   │         ┌────┘
   ▼         ▼
room_members
   │
   │
   ▼
events
   │
   ├── m.room.message ──► messages
   ├── m.room.member
   ├── m.room.name
   ├── m.room.avatar
   ├── m.room.tombstone
   ├── m.room.encrypted
   └── ...
```

------------------------------------------------------------------------

# Datenbankschema

``` sql
PRAGMA foreign_keys = ON;

-- ============================================================
-- Accounts
-- Der lokale Matrix-Account, zu dem die Daten gehören.
-- Dadurch funktioniert die DB auch mit mehreren Accounts.
-- ============================================================

CREATE TABLE accounts (
    id              INTEGER PRIMARY KEY,
    user_id         TEXT NOT NULL UNIQUE,
    homeserver      TEXT NOT NULL,
    display_name    TEXT,
    avatar_url      TEXT
);


-- ============================================================
-- Users / Kontakte
-- Globale bekannte Matrix-Benutzer.
-- ============================================================

CREATE TABLE contacts (
    user_id         TEXT PRIMARY KEY,

    display_name    TEXT,
    avatar_url      TEXT,

    -- optionale lokale Metadaten des Clients
    alias           TEXT,
    is_contact      INTEGER NOT NULL DEFAULT 0,
    is_blocked      INTEGER NOT NULL DEFAULT 0,

    updated_at      INTEGER
);


-- ============================================================
-- Räume
-- ============================================================

CREATE TABLE rooms (
    room_id             TEXT PRIMARY KEY,

    account_id          INTEGER NOT NULL,

    room_version        TEXT,
    room_type           TEXT,

    name                TEXT,
    topic               TEXT,
    avatar_url          TEXT,

    -- join / invite / leave / ban
    membership          TEXT,

    -- Client-Klassifikation
    is_direct           INTEGER NOT NULL DEFAULT 0,

    -- m.room.tombstone
    replacement_room_id TEXT,

    -- Verschlüsselung aktiviert?
    is_encrypted        INTEGER NOT NULL DEFAULT 0,

    -- Sync-Daten
    prev_batch          TEXT,

    created_at          INTEGER,
    updated_at          INTEGER,

    FOREIGN KEY (account_id)
        REFERENCES accounts(id)
        ON DELETE CASCADE
);


CREATE INDEX idx_rooms_account
    ON rooms(account_id);

CREATE INDEX idx_rooms_replacement
    ON rooms(replacement_room_id);


-- ============================================================
-- Mitgliedschaften
-- aktueller Membership-State eines Raumes
-- ============================================================

CREATE TABLE room_members (
    room_id         TEXT NOT NULL,
    user_id         TEXT NOT NULL,

    membership      TEXT NOT NULL,

    display_name    TEXT,
    avatar_url      TEXT,

    -- Event, das diesen aktuellen State erzeugt hat
    event_id        TEXT,

    updated_at      INTEGER,

    PRIMARY KEY (room_id, user_id),

    FOREIGN KEY (room_id)
        REFERENCES rooms(room_id)
        ON DELETE CASCADE,

    FOREIGN KEY (user_id)
        REFERENCES contacts(user_id)
);


CREATE INDEX idx_room_members_user
    ON room_members(user_id);


-- ============================================================
-- Events
--
-- Zentrale Event-Tabelle.
-- Hier landen Timeline- und State-Events.
-- ============================================================

CREATE TABLE events (
    event_id            TEXT PRIMARY KEY,

    room_id             TEXT NOT NULL,

    type                TEXT NOT NULL,
    sender              TEXT,

    origin_server_ts    INTEGER,

    -- NULL bei normalen Timeline Events
    -- "" oder anderer Wert bei State Events
    state_key           TEXT,

    -- kompletter Event-Content
    content_json        TEXT NOT NULL,

    -- unsigned / relations / weitere Rohdaten
    unsigned_json       TEXT,

    -- lokaler Status
    local_timestamp     INTEGER,

    FOREIGN KEY (room_id)
        REFERENCES rooms(room_id)
        ON DELETE CASCADE
);


CREATE INDEX idx_events_room_time
    ON events(room_id, origin_server_ts);

CREATE INDEX idx_events_room_type
    ON events(room_id, type);

CREATE INDEX idx_events_sender
    ON events(sender);

CREATE INDEX idx_events_state
    ON events(room_id, type, state_key);


-- ============================================================
-- Nachrichten
--
-- Materialisierte Darstellung von m.room.message.
--
-- event_id ist gleichzeitig FK auf das ursprüngliche Event.
-- ============================================================

CREATE TABLE messages (
    event_id             TEXT PRIMARY KEY,

    room_id              TEXT NOT NULL,
    sender               TEXT NOT NULL,

    msgtype              TEXT NOT NULL,

    body                 TEXT,

    formatted_body       TEXT,
    format               TEXT,

    media_url            TEXT,

    -- Reply
    reply_to_event_id    TEXT,

    -- Thread
    thread_root_event_id TEXT,

    -- Edit
    replaces_event_id    TEXT,

    -- Client-Zustand
    is_edited            INTEGER NOT NULL DEFAULT 0,
    is_redacted          INTEGER NOT NULL DEFAULT 0,

    timestamp            INTEGER NOT NULL,

    FOREIGN KEY (event_id)
        REFERENCES events(event_id)
        ON DELETE CASCADE,

    FOREIGN KEY (room_id)
        REFERENCES rooms(room_id)
        ON DELETE CASCADE
);


CREATE INDEX idx_messages_room_time
    ON messages(room_id, timestamp);

CREATE INDEX idx_messages_sender
    ON messages(sender);

CREATE INDEX idx_messages_reply
    ON messages(reply_to_event_id);

CREATE INDEX idx_messages_thread
    ON messages(thread_root_event_id);
```

------------------------------------------------------------------------

# Warum `events` und `messages` getrennt?

Das ist für Matrix eine wichtige Designentscheidung.

Matrix liefert beispielsweise:

``` text
events
│
├── m.room.message
├── m.room.member
├── m.room.name
├── m.room.topic
├── m.room.avatar
├── m.room.create
├── m.room.power_levels
├── m.room.tombstone
├── m.room.encrypted
├── m.reaction
└── ...
```

Man möchte nicht für jeden möglichen Event-Typ eine komplett eigene
Datenstruktur bauen.

Deshalb speichert `events` zunächst das **Matrix-Event möglichst
verlustfrei**.

Beispiel:

``` sql
INSERT INTO events (
    event_id,
    room_id,
    type,
    sender,
    origin_server_ts,
    content_json
)
VALUES (
    '$abc123',
    '!room:example.org',
    'm.room.message',
    '@alice:example.org',
    1760000000000,
    '{
        "msgtype": "m.text",
        "body": "Hallo Bob!"
    }'
);
```

Dann werden für häufig benötigte UI-Daten die relevanten Felder
extrahiert:

``` sql
INSERT INTO messages (
    event_id,
    room_id,
    sender,
    msgtype,
    body,
    timestamp
)
VALUES (
    '$abc123',
    '!room:example.org',
    '@alice:example.org',
    'm.text',
    'Hallo Bob!',
    1760000000000
);
```

Die UI muss dadurch nicht für jede Anzeige JSON parsen.

------------------------------------------------------------------------

# State Events materialisieren

Nehmen wir folgendes Event:

``` json
{
  "type": "m.room.name",
  "state_key": "",
  "content": {
    "name": "Projekt Alpha"
  }
}
```

Das Event bleibt vollständig in:

``` text
events
```

Der aktuelle Zustand wird zusätzlich nach:

``` text
rooms.name
```

geschrieben.

Damit ergibt sich:

``` text
m.room.name
      │
      ├────► events
      │      vollständige Historie
      │
      └────► rooms.name
             aktueller Zustand
```

Dasselbe Prinzip gilt beispielsweise für:

``` text
m.room.name
        → rooms.name

m.room.topic
        → rooms.topic

m.room.avatar
        → rooms.avatar_url

m.room.tombstone
        → rooms.replacement_room_id

m.room.encryption
        → rooms.is_encrypted

m.room.member
        → room_members
```

Damit bekommt man eine schnelle UI, ohne die Event-Historie zu
verlieren.

------------------------------------------------------------------------

# `room_members`

Den Display Name eines Users sollte man **nicht nur in `contacts`
speichern**.

Beispielsweise könnte:

``` text
@alice:matrix.org
```

global den Display Name

``` text
Alice Müller
```

besitzen.

Im Raum könnten die relevanten Membership-Daten aber anders aussehen:

``` text
contacts
─────────────────────────
@alice:matrix.org
Alice Müller
avatar.jpg


room_members
────────────────────────────────────
!firma:example.org
@alice:matrix.org
Alice (Support)
support-avatar.jpg
```

Beim Rendern einer Nachricht kann dann beispielsweise folgende Abfrage
verwendet werden:

``` sql
SELECT
    m.event_id,
    m.body,
    m.timestamp,

    rm.display_name,
    rm.avatar_url

FROM messages m

LEFT JOIN room_members rm
    ON rm.room_id = m.room_id
   AND rm.user_id = m.sender

WHERE m.room_id = ?

ORDER BY m.timestamp;
```

## Historische Profile

`room_members` enthält dabei den **aktuellen** Zustand.

Wenn Alice heute `"Alice B."` heißt, eine zwei Jahre alte Nachricht aber
historisch mit `"Alice Müller"` dargestellt werden soll, benötigt der
Client zusätzlich historischen State beziehungsweise einen
Event-Snapshot.

Das kann später ergänzt werden, wenn historische Profile korrekt
dargestellt werden sollen.

------------------------------------------------------------------------

# Direktchats

Ein Direktchat sollte nicht als eigene Raumart modelliert werden.

Eine einfache Klassifikation kann zunächst über:

``` text
rooms.is_direct
```

erfolgen:

``` text
room_id                 is_direct
─────────────────────────────────
!abc:example.org         1
!projekt:example.org     0
```

Für eine genauere Abbildung der Matrix-Semantik sollte jedoch die
eigentliche `m.direct`-Zuordnung gespeichert werden.

Dafür bietet sich eine eigene Tabelle an:

``` sql
CREATE TABLE direct_rooms (
    account_id      INTEGER NOT NULL,
    user_id         TEXT NOT NULL,
    room_id         TEXT NOT NULL,

    PRIMARY KEY (account_id, user_id, room_id),

    FOREIGN KEY (account_id)
        REFERENCES accounts(id)
        ON DELETE CASCADE,

    FOREIGN KEY (room_id)
        REFERENCES rooms(room_id)
        ON DELETE CASCADE
);
```

Damit kann beispielsweise abgebildet werden:

``` text
@alice:example.org
        │
        ├── !room1:example.org
        └── !room2:example.org
```

Das entspricht besser der Struktur von `m.direct` als ein einfaches
Boolean.

------------------------------------------------------------------------

# Room-Upgrades

`m.room.tombstone` lässt sich ebenfalls sauber abbilden.

Ein alter Raum:

``` text
!old:example.org
```

kann beispielsweise folgendes Event enthalten:

``` json
{
  "type": "m.room.tombstone",
  "state_key": "",
  "content": {
    "body": "Room upgraded",
    "replacement_room": "!new:example.org"
  }
}
```

Das Event wird zunächst vollständig in `events` gespeichert.

Zusätzlich wird `replacement_room` materialisiert:

``` text
rooms

room_id              replacement_room_id
─────────────────────────────────────────────
!old:example.org      !new:example.org
!new:example.org      NULL
```

Damit ist die Abfrage sehr einfach:

``` sql
SELECT replacement_room_id
FROM rooms
WHERE room_id = ?;
```

Wenn:

``` text
replacement_room_id IS NOT NULL
```

weiß der Client, dass der Raum ersetzt wurde.

------------------------------------------------------------------------

# Lokale und noch nicht gesendete Nachrichten

Für einen echten Chatclient reicht `event_id PRIMARY KEY` langfristig
nicht aus.

Wenn eine Nachricht gesendet wird, besitzt sie möglicherweise zunächst
nur eine lokale Transaction-ID:

``` text
txn-847293
```

Erst nach erfolgreichem Senden erhält man beispielsweise:

``` text
$serverEventId123
```

Deshalb sollte das Nachrichtenmodell zusätzlich lokale
Sendeinformationen besitzen.

Beispielsweise:

``` sql
ALTER TABLE messages
ADD COLUMN transaction_id TEXT;

ALTER TABLE messages
ADD COLUMN send_state TEXT NOT NULL DEFAULT 'sent';
```

Mögliche Zustände:

``` text
pending
sending
sent
failed
```

Damit kann die UI sofort darstellen:

``` text
Ich: Hallo!                 ◷
```

während die Nachricht noch übertragen wird.

Nach erfolgreicher Übertragung:

``` text
Ich: Hallo!                 ✓
```

------------------------------------------------------------------------

# Gesamtarchitektur

``` text
SQLite
│
├── accounts
│     └── eigener Matrix-Account
│
├── contacts
│     └── bekannte Matrix-User
│
├── rooms
│     ├── Name
│     ├── Avatar
│     ├── Topic
│     ├── Room Version
│     ├── Room Type
│     ├── Encryption
│     └── Replacement Room
│
├── direct_rooms
│     └── m.direct-Zuordnung
│
├── room_members
│     ├── Membership
│     ├── Display Name
│     └── Avatar
│
├── events
│     └── vollständige Matrix-Events
│
└── messages
      ├── Text
      ├── msgtype
      ├── Medien
      ├── Replies
      ├── Threads
      ├── Edits
      └── Sendestatus
```

------------------------------------------------------------------------

# Architekturprinzip

Der zentrale Architekturgedanke ist:

> **`events` als Source of Truth + materialisierte Tabellen für schnelle
> Abfragen.**

Das passt gut zu Matrix, weil das Protokoll selbst eventbasiert ist.

Gleichzeitig muss die Chat-UI dadurch nicht ständig sämtliche
Event-JSONs rekonstruieren oder parsen.

Die Rollen der Tabellen sind damit klar getrennt:

``` text
events
    ↓
vollständige, möglichst verlustfreie Matrix-Daten

rooms / room_members / messages
    ↓
materialisierte, für den Client optimierte Ansichten
```

Dadurch bleibt die Persistenz flexibel gegenüber neuen
Matrix-Event-Typen, während häufige UI-Abfragen performant und einfach
bleiben.
