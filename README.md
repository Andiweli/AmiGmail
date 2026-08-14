# ![Logo](https://github.com/Andiweli/AmiGmail/blob/main/images/amigmail-icon.jpg) AmiGmail 1.2

AmiGmail ist ein quelloffener Entwurf für einen Gmail-Client auf AmigaOS 3.2
(68k) mit ReAction, IMAP, SMTP und AmiSSL. Das Repository enthält die
Protokollschicht, MIME-/Codec-Bausteine, einen getrennten Netzwerkprozess und
ein dreigeteiltes Workbench-Fenster. Der aktuelle Quellstand entspricht Version 1.2;
er sollte vor produktiver Nutzung mit dem eigenen Gmail-Konto getestet werden.

## Enthaltener Funktionsumfang

- IMAP-Verbindung über implizites TLS auf `imap.gmail.com:993`
- Anmeldung per XOAUTH2-Token oder App-Passwort über `AUTHENTICATE PLAIN`
- Gmail-Labels über `LIST ... RETURN (SPECIAL-USE)`, mit `XLIST`-Fallback
- Modified UTF-7 für IMAP-Mailboxnamen
- datumsbegrenzter Header-Abruf per UID; Treffer werden in 100-UID-Paketen geladen
- Gmail-Erweiterungen `X-GM-MSGID`, `X-GM-THRID` und `X-GM-LABELS`
- Markieren als gelesen/ungelesen, Löschen und Labeloperationen per IMAP
- Verfassen und Antworten per SMTP über direktes TLS auf `smtp.gmail.com:465`
- vorhandene Gmail-Entwürfe im Ordner **Entwürfe** über **Bearbeiten/Edit** öffnen; erneutes Speichern ersetzt den bisherigen serverseitigen Entwurf
- RFC-5322-kompatible CRLF-Zeilen, Dot-Stuffing, `In-Reply-To` und `References`
- MIME-Header, RFC 2047, Base64, Quoted-Printable, `text/plain` und einfacher
  HTML-zu-Text-Fallback
- OAuth-2.0-Hilfsfunktionen für Authorization Code + PKCE und Token-Refresh
- verschlüsselte Kontogeheimnisse mit PBKDF2-HMAC-SHA256 und AES-256-GCM
- eigener Netzwerkprozess mit Exec Message Ports; keine blockierenden
  Netzwerkzugriffe im GUI-Task
- defensive Größenlimits, Eingabeprüfung und Nullsetzen sensibler Puffer

## Oberfläche

Das ReAction-Hauptfenster ist in drei Arbeitsbereiche gegliedert:

Das ReAction-Hauptfenster ist in drei Arbeitsbereiche gegliedert:

1. links: Gmail-Systemordner und darunter die eigenen Labels als Ordnerbaum,
2. rechts oben: Nachrichtenliste mit Absender, Betreff, Datum und Größe,
3. rechts unten: Nur-Lese-Vorschau; das Öffnen markiert die Mail als gelesen.

![Screenshot version 1.1](https://github.com/Andiweli/AmiGmail/blob/main/images/amigmail.jpg)

## 📧 Features

Darüber liegen die Aktionen **Neue Mail**, **Abrufen**, **Antworten**
(im Ordner **Entwürfe**: **Bearbeiten/Edit**), **Löschen**, **Verschieben** und
**Un/Gelesen**; darunter befindet sich eine Statuszeile. Mit Umschalt/Shift lassen sich mehrere Mails für Lesestatus und
Löschen markieren.

Die Oberfläche ist zweisprachig. Ist AmigaOS auf **Deutsch** eingestellt,
verwendet AmiGmail die deutsche Oberfläche. Bei jeder anderen Systemsprache
wird **Englisch** verwendet. Die Auswahl erfolgt beim Programmstart über die
AmigaOS-Systemvariable `LanguageName`.

## Voraussetzungen

Zielsystem:

- AmigaOS 3.2 auf 68020 oder neuer
- TCP/IP-Stack mit `bsdsocket.library` V4
- AmiSSL v5 samt `AmiSSL:`-Assign und aktuellem CA-Bundle
- ReAction-Klassen aus AmigaOS 3.2
- korrekte Systemzeit für TLS-Zertifikatsprüfung

Build-Host:

- Windows mit MSYS2/MSYS oder ein POSIX-kompatibler Host
- `make`, `zip` und ein m68k-AmigaOS-Crosscompiler (`m68k-amigaos-gcc`)
- AmigaOS 3.2 NDK/ReAction-Includes
- AmiSSL-v5-SDK für m68k

## MSYS-/AmigaGCC-Build

Das Makefile leitet `AMIGA_PREFIX` standardmäßig aus dem tatsächlich im `PATH`
gefundenen `m68k-amigaos-gcc` ab. Liegt der Compiler beispielsweise unter
`/c/amiga-gcc/bin/m68k-amigaos-gcc`, wird automatisch
`/c/amiga-gcc/m68k-amigaos/ndk-include` als NDK-Pfad verwendet.

Ein normaler Build ist daher zunächst einfach:

```sh
make clean
make
```

Falls die SDKs an Sonderpfaden liegen, können alle Pfade weiterhin ohne
Änderung am Makefile explizit gesetzt werden:

```sh
cd AmiGmail
make check-env \
  AMIGA_PREFIX=/c/amiga-gcc \
  NDK_INC=/c/amiga-gcc/m68k-amigaos/ndk-include \
  REACTION_SDK=/c/amiga-gcc/m68k-amigaos/ndk-include \
  AMISSL_SDK=/c/amiga-sdk/AmiSSL/Developer

make release \
  CC=m68k-amigaos-gcc \
  AMIGA_PREFIX=/c/amiga-gcc \
  NDK_INC=/c/amiga-gcc/m68k-amigaos/ndk-include \
  REACTION_SDK=/c/amiga-gcc/m68k-amigaos/ndk-include \
  AMISSL_SDK=/c/amiga-sdk/AmiSSL/Developer
```

Das Ziel ist `bin/AmiGmail`. Die Voreinstellungen verwenden `-m68020` und
`-msoft-float`. `src/tls.c` öffnet `bsdsocket.library`,
`amisslmaster.library` und AmiSSL ausdrücklich. Deshalb wird
`libamisslauto.a` absichtlich nicht zusätzlich gelinkt. Nur falls später
AmiSSL-Funktionen als Funktionszeiger benötigt werden, kann je nach SDK
`AMISSL_EXTRA_LIBS=-lamisslstubs` gesetzt werden.

`AMISSL_SDK` zeigt auf das AmiSSL-`Developer`-Verzeichnis, das direkt
`include/` und `lib/` enthält. Das Makefile erkennt zusätzlich automatisch den
üblichen lokalen Pfad `~/dev/amiga/sdk/AmiSSL-5.27-SDK/AmiSSL/Developer`, wenn
dort `include/proto/amissl.h` vorhanden ist. Ein explizit beim `make` gesetzter
`AMISSL_SDK`-Wert hat immer Vorrang.

Die tatsächliche Verzeichnisstruktur eines AmigaGCC-Pakets kann abweichen.
Entscheidend sind die Verzeichnisse mit den NDK-/ReAction-Headern sowie
`amissl/`, `openssl/` und den m68k-Linkbibliotheken des AmiSSL-SDKs.

## Hosttests

Die portablen Module lassen sich unabhängig vom Amiga-SDK prüfen:

```sh
make host-test
make host-check
```

`host-test` testet Base64, Quoted-Printable, Modified UTF-7, fragmentierte
IMAP-Antworten und Literale, MIME/RFC 2047, SMTP-Dot-Stuffing, OAuth-PKCE,
SHA-256 und Kontovalidierung. `host-check` kompiliert alle Quelldateien mit
strengen Warnungen; Amiga-spezifische Zweige werden dabei durch Host-Stubs
ersetzt. Das entstehende Prüfbinary ist nicht als Linux-Anwendung gedacht und
wird daher nicht gestartet.

## Gmail-Anmeldung

### Empfohlen: OAuth 2.0

1. In Google Cloud ein Projekt und einen OAuth-Client vom Typ
   **Desktopanwendung** anlegen.
2. Die Gmail-API aktivieren und den Scope `https://mail.google.com/` zulassen.
3. `config/oauth_client_config.h.example` nach
   `include/oauth_client_config.h` kopieren und die Client-ID eintragen.
4. Authorization Code + PKCE verwenden. Die Bibliotheksfunktionen erzeugen
   Verifier, Challenge und State, bauen die Autorisierungs-URL, tauschen den
   Code ein und erneuern Access Tokens.

Der vollständige ReAction-Anmeldeassistent einschließlich lokalem
Loopback-Callback ist noch nicht implementiert. Ein Client-Secret gehört bei
einer Desktopanwendung nicht als vermeintliches Geheimnis in eine öffentliche
68k-Binärdatei.

### Alternative: Gmail-App-Passwort

Für ein Google-Konto mit aktivierter Bestätigung in zwei Schritten kann ein
16-stelliges App-Passwort verwendet werden. Es ersetzt nicht das normale
Google-Passwort. AmiGmail validiert das Format und überträgt es nur innerhalb
der TLS-Verbindung.

Der ReAction-Kontodialog speichert das App-Passwort verschlüsselt und kann den
Posteingang auf Wunsch beim Programmstart automatisch abrufen. Zusätzlich kann
**Periodischer Abruf (5 Min.)** aktiviert werden. AmiGmail prüft dann alle fünf
Minuten den Posteingang und lädt nach dem ersten Abruf nur UIDs, die neuer als
die zuletzt bekannte Inbox-UID sind. Ein gerade geöffneter anderer Ordner wird
dadurch nicht umgeschaltet.

## Konfigurations- und Sicherheitsmodell

`src/storage.c` definiert das Format `AMIGMAIL-ACCOUNT-1`. Anzeigename und
Serverdaten sind normale Metadaten. Refresh Token beziehungsweise App-Passwort
werden nur mit einem Master-Passwort gespeichert; der Schlüssel entsteht über
PBKDF2-HMAC-SHA256 (100.000 Iterationen), die Nutzdaten werden mit
AES-256-GCM geschützt. Geschrieben wird zunächst eine `.new`-Datei und danach
umbenannt.

Beim Start kann das gespeicherte Konto mit dem Master-Passwort entsperrt werden.

## Quellstruktur

| Bereich | Dateien | Aufgabe |
|---|---|---|
| Anwendung/GUI | `app.c`, `gui.c`, `i18n.c` | Lebenszyklus, ReAction-Fenster und Deutsch/Englisch-Umschaltung |
| Asynchronität | `network_task.c` | eigener Prozess und Message Ports |
| Gmail/IMAP | `imap.c`, `imap_parser.c` | Anmeldung, Labels, UIDs, Literale |
| Versand | `smtp.c` | TLS-SMTP und Antworten |
| Nachricht | `mime.c`, `codec.c`, `charset.c` | MIME, Transferkodierungen, Text |
| Sicherheit | `tls.c`, `oauth.c`, `crypto.c`, `storage.c` | AmiSSL, PKCE, SHA-256, Secrets |
| Tests | `tests/test_main.c` | portable Protokoll- und Codec-Tests |

Weitere technische Details stehen in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) und
[docs/OAUTH_SETUP.md](docs/OAUTH_SETUP.md).

## Bekannte Grenzen

- kein verifizierter 68k-Build in der vorliegenden Umgebung
- der OAuth-Callback-Assistent fehlt; der aktuelle GUI-Pfad verwendet ein
  Gmail-App-Passwort
- SMTP STARTTLS auf Port 587 ist noch nicht implementiert; Port 465 wird genutzt
- nur Textantworten mit Anlagen, keine HTML-Komposition
- kein Offline-Cache und keine Volltextsuche
- einfache MIME-Auswahl statt vollständig verschachteltem MIME-Baum

## Referenzen

- [Gmail: IMAP, POP und SMTP](https://developers.google.com/workspace/gmail/imap/imap-smtp)
- [Gmail: IMAP-Erweiterungen und Labels](https://developers.google.com/workspace/gmail/imap/imap-extensions)
- [Google OAuth 2.0 für installierte Anwendungen](https://developers.google.com/identity/protocols/oauth2/native-app)
- [AmiSSL-Projekt und SDK-Hinweise](https://github.com/jens-maus/amissl)

![Configuration of AmiGmail](https://github.com/Andiweli/AmiGmail/blob/main/images/amigmail-config.jpg)

## 📧 Legal

AmiGmail is an independent, non-commercial hobby project and is **not affiliated with, developed by, supported by or sponsored by Google LLC**. **Gmail** is a trademark of Google LLC. **Amiga** and **AmigaOS** are trademarks of their respective owners. AmiSSL and other third-party components remain subject to their respective licenses.

Copyright © Andreas 'Andiweli' Stürmer.
