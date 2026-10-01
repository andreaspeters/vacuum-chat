# Direktchat vs. normaler Raum in Matrix

Der entscheidende Punkt bei Matrix ist:

> **Technisch ist auch ein Direktchat ein Raum.**

Ein Direktchat (DM) ist also kein fundamental anderes Objekt als ein
selbst angelegter privater Raum. Die Unterscheidung liegt hauptsächlich
in zusätzlichen Metadaten und darin, wie der Matrix-Client den Raum
interpretiert.

------------------------------------------------------------------------

## 1. Direktchat mit einer Person

Wenn du beispielsweise mit Alice einen Direktchat startest, entsteht
technisch ein normaler Matrix-Raum:

``` text
Direktchat mit Alice
└─ Room !abc:example.org
   ├─ Mitglieder: Du + Alice
   ├─ is_direct: true
   └─ dein Account Data:
      m.direct:
        "@alice:example.org":
          ["!abc:example.org"]
```

### `is_direct`

Beim Erstellen oder Einladen kann angegeben werden, dass die Einladung
für einen Direktchat gedacht ist.

Das ist ein Hinweis für Clients und kein eigener Room Type.

### `m.direct`

Zusätzlich kann der Client in den Account-Daten des Benutzers
hinterlegen:

``` json
{
  "@alice:example.org": [
    "!abc:example.org"
  ]
}
```

Das bedeutet sinngemäß:

> Für mich ist `!abc:example.org` ein Direktchat mit
> `@alice:example.org`.

Wichtig dabei:

**`m.direct` gehört zum jeweiligen Benutzer und nicht zum globalen
Zustand des Raums.**

Daher kann ein Raum von einem Benutzer als Direktchat betrachtet werden,
während ein anderer Benutzer denselben Raum nicht zwingend als
Direktchat klassifiziert.

------------------------------------------------------------------------

## 2. Selbst angelegter privater Raum

Du kannst stattdessen einen normalen Raum erstellen:

``` text
Raum "Projekt X"
└─ Room !xyz:example.org
   ├─ m.room.name   = "Projekt X"
   ├─ m.room.avatar = ...
   ├─ Mitglieder:
   │  ├─ Du
   │  └─ Alice
   └─ nicht als DM markiert
```

Auch wenn sich dort nur zwei Personen befinden, wird daraus nicht
automatisch ein Direktchat.

Der Raum kann beispielsweise einen eigenen Namen besitzen:

``` json
{
  "type": "m.room.name",
  "state_key": "",
  "content": {
    "name": "Projekt X"
  }
}
```

und einen eigenen Avatar:

``` json
{
  "type": "m.room.avatar",
  "state_key": "",
  "content": {
    "url": "mxc://example.org/abcdef"
  }
}
```

------------------------------------------------------------------------

## 3. Vergleich

  -----------------------------------------------------------------------
  Eigenschaft             Direktchat mit Alice    Normaler privater Raum
  ----------------------- ----------------------- -----------------------
  Matrix-Objekt           Room                    Room

  eigener Room Type       Nein                    Nein
  notwendig                                       

  exakt 2 Personen        Nein                    Nein
  vorgeschrieben                                  

  als DM gekennzeichnet   `m.direct` /            normalerweise nein
                          `is_direct`             

  eigener Name möglich    Ja                      Ja

  eigener Avatar möglich  Ja                      Ja

  weitere Personen        Ja                      Ja
  einladbar                                       

  typische                Personen /              Räume
  Client-Darstellung      Direktnachrichten       

  `m.room.name` möglich   Ja                      Ja

  `m.room.avatar` möglich Ja                      Ja
  -----------------------------------------------------------------------

------------------------------------------------------------------------

# Name und Avatar bestimmen nicht den Raumtyp

Ein wichtiger Punkt ist:

> **Ein eigener Name oder Avatar macht einen Raum nicht zu einem
> Gruppenraum.**

`m.room.name` und `m.room.avatar` sind lediglich State Events.

Ein Direktchat kann technisch ebenfalls enthalten:

``` text
m.room.name
m.room.avatar
```

Clients können bei Direktchats allerdings entscheiden, stattdessen
beispielsweise automatisch den Namen und Avatar der anderen Person
darzustellen.

------------------------------------------------------------------------

# Zwei Mitglieder bedeuten nicht automatisch Direktchat

Ein Client sollte **nicht** folgende Logik verwenden:

``` text
Mitglieder == 2
→ Direktchat
```

Denn dieser Raum:

``` text
!room:example.org

Mitglieder:
- @alice:example.org
- @bob:example.org
```

kann trotzdem ein normaler privater Raum sein.

Umgekehrt kann ein Direktchat beispielsweise so aussehen:

``` text
!room:example.org

Mitglieder:
- @alice:example.org
- @bob:example.org
- @bot:example.org
```

und trotzdem von einem Benutzer als Direktchat klassifiziert werden.

------------------------------------------------------------------------

# Room Type ist davon unabhängig

Die DM-Eigenschaft sollte außerdem nicht mit dem Matrix-**Room Type**
verwechselt werden.

Ein normaler Chatraum besitzt typischerweise keinen speziellen `type`:

``` json
{
  "type": "m.room.create",
  "state_key": "",
  "content": {
    "creator": "@alice:example.org"
  }
}
```

Ein Space dagegen besitzt beispielsweise:

``` json
{
  "type": "m.room.create",
  "state_key": "",
  "content": {
    "type": "m.space"
  }
}
```

Damit ergeben sich zwei unterschiedliche Konzepte:

``` text
Room Type
│
├── normaler Room
├── m.space
├── m.policy
└── benutzerdefinierter Room Type

DM-Klassifikation
│
├── als Direktchat betrachtet
└── nicht als Direktchat betrachtet
```

Ein Direktchat ist daher **kein eigener Room Type**.

------------------------------------------------------------------------

# Mentales Modell

Am einfachsten kann man Matrix ungefähr so betrachten:

``` text
                     Matrix Room
                         │
            ┌────────────┴────────────┐
            │                         │
        Room Type                Metadaten /
                                 Interpretation
            │                         │
     ┌──────┼──────┐           ┌─────┴─────┐
     │      │      │           │           │
 normal  m.space  m.policy     DM      normaler
                               │        Chatraum
                               │
                            m.direct
```

Damit sind beispielsweise folgende Situationen möglich:

``` text
Room A
├─ normaler Room Type
├─ 2 Mitglieder
└─ als DM markiert

Room B
├─ normaler Room Type
├─ 2 Mitglieder
├─ Name: "Projekt X"
├─ Avatar
└─ nicht als DM markiert

Room C
├─ normaler Room Type
├─ 5 Mitglieder
├─ Name: "Entwicklung"
└─ normaler Gruppenraum

Room D
├─ type: m.space
├─ Name: "Meine Firma"
└─ enthält/verknüpft andere Räume
```

------------------------------------------------------------------------

# Für die Entwicklung eines Matrix-Clients

Wenn du einen eigenen Client entwickelst, solltest du einen Direktchat
**nicht anhand der Mitgliederzahl erkennen**.

Stattdessen solltest du insbesondere die Account Data des Benutzers
berücksichtigen:

``` text
m.direct
```

und beim Erstellen beziehungsweise Einladen eines Direktchats
entsprechend mit:

``` text
is_direct
```

arbeiten.

Die grundlegende Logik ist also:

``` text
Room
 │
 ├─ Welchen Room Type hat er?
 │
 │   ├─ m.space
 │   ├─ m.policy
 │   ├─ custom type
 │   └─ kein type → normaler Room
 │
 └─ Wie interpretiert der Benutzer den Room?
     │
     ├─ in m.direct → Direktchat
     └─ nicht in m.direct → normaler Raum
```

## Kurzfassung

**Direktchat:**

``` text
normaler Matrix-Raum
+
DM-Metadaten
+
Client-Interpretation
```

**Normaler privater Chatraum:**

``` text
normaler Matrix-Raum
+
keine DM-Klassifikation
```

Name, Avatar und Mitgliederzahl entscheiden dabei **nicht**, ob es sich
um einen Direktchat handelt.
