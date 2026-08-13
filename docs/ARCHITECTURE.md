# Architektur

## Ziele

AmiGmail trennt die langsamen, zustandsbehafteten Netzwerkprotokolle von der
Intuition-/ReAction-Ereignisschleife. Die GUI sendet kleine Aufträge an einen
Netzwerkprozess. Dieser hält IMAP-, TLS- und OAuth-Zustand und antwortet über
einen eigenen Message Port. Nutzdaten werden jeweils vom Empfänger freigegeben.

## Komponentenfluss

```text
ReAction GUI
    |  AmgNetMessage (Exec Message Port)
    v
Netzwerkprozess
    |-- OAuth/HTTPS ---- AmiSSL ---- Google OAuth
    |-- IMAP ----------- AmiSSL ---- imap.gmail.com:993
    `-- SMTP ----------- AmiSSL ---- smtp.gmail.com:465
```

Der Datenpfad innerhalb des Netzwerkprozesses lautet:

```text
TLS-Blöcke -> inkrementeller IMAP-Parser -> Antwortpuffer
Antwortpuffer -> MIME-/Header-Parser -> GUI-Modell -> ListBrowser/TextField
```

Der letzte Schritt vom Antwortpuffer zum GUI-Modell ist im Developer Preview
noch nicht implementiert.

## Zuständigkeiten

### GUI-Task

- öffnet und schließt ReAction-Klassen
- verarbeitet ausschließlich Fenster-, Gadget-, Menü- und Netzwerksignale
- startet niemals eine blockierende Socket- oder TLS-Operation
- besitzt ReAction-Objekte und ListBrowser-Knoten

### Netzwerkprozess

- öffnet AmiSSL und hält die IMAP-Sitzung
- aktualisiert OAuth-Access-Tokens aus einem Refresh Token
- führt jeweils einen serialisierten IMAP-/SMTP-Auftrag aus
- liefert Ergebniscode, deutschsprachige Fehlermeldung und optionalen Puffer

### Protokollmodule

- `imap_parser.c` kennt nur Bytes, CRLF und Literallängen
- `imap.c` kennt Gmail-Kommandos, UIDs und Labelsemantik
- `mime.c` dekodiert Nachrichteninhalt ohne Kenntnis von ReAction
- `smtp.c` erzeugt und überträgt Antworten
- `tls.c` kapselt bsdsocket und AmiSSL

## Speicher- und Größengrenzen

- maximale IMAP-Zeile: 8 KiB
- maximale einzelne Nachricht: 8 MiB
- maximal 256 Labels
- maximal 2.048 Header
- standardmäßig 50 Nachrichten pro Seite
- Netzwerk-Read-/Write-Timeout: 30 Sekunden

Die Grenzen vermeiden unkontrolliertes Wachstum auf klassischen Amiga-Systemen.
Eine produktive Version sollte große Bodies zusätzlich in temporäre Dateien
streamen, anstatt sie vollständig im RAM zu halten.

## Fehlerbehandlung

Öffentliche Funktionen liefern einen `AmgResult` und können einen `AmgError`
mit verständlichem Text füllen. Netzwerkfehler gehen als Antwortnachrichten an
die GUI. Geheimnisse werden vor dem Freigeben mit `amg_secure_clear` genullt.

## Nächste Integrationsschritte

1. Kontodialog und Master-Passwort-Dialog als modale ReAction-Fenster.
2. OAuth-Loopback-Listener mit Browser-URL und manueller Code-Fallback-Eingabe.
3. Parser für die UID-FETCH-Metadaten in ein kompaktes Nachrichtenmodell.
4. ListBrowser-Aktualisierung, Selektion, Preview und Seitennavigation.
5. Antworteditor sowie Ziel-Label-Chooser.
6. End-to-End-Tests gegen ein separates Gmail-Testkonto.

