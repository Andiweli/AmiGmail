# AmiGmail Changelog

## 1.7 – 2026-08-17

- Programmversion auf 1.7 aktualisiert
- Start-Benachrichtigung korrigiert: trifft während beendetem AmiGmail die erste neue Mail ein und lag die gespeicherte Inbox-Basis bei UID 0, wird sie beim nächsten Programmstart nun korrekt als neu erkannt
- dadurch wird der konfigurierte Benachrichtigungston auch für diese erste neue Mail nach dem Start zuverlässig abgespielt
- Workbench-Programmicon auf StackSize 100000 erhöht, um den auf dem getesteten AmigaOS-System beobachteten zu kleinen 4096-Byte-Stack zu vermeiden


## 1.6 – 2026-08-15

- Programmversion auf 1.6 aktualisiert; Kontakte-/Adressbuch-Funktionsumfang als offizieller Release-Stand finalisiert
- v58 Fokus-/TAB-Fix: Kontakteditor aktiviert das erste String-Gadget ueber `ActivateLayoutGadget()` innerhalb des ReAction-Layouts; TAB wechselt stabil durch alle sieben Kontaktfelder

- v59 UI-Polish: Kontaktlisten verwenden nun dieselbe kompakte Zeilenhöhe wie Mail-/Label-Listen (`Screenfont-Höhe + 2`), damit Unterlängen wie `g`, `p`, `q` und `y` vollständig sichtbar bleiben
- Vorname und Nachname haben identische Spaltenbreite; `LISTBROWSER_AutoFit` wurde für Kontaktlisten entfernt, damit ReAction die vorgesehenen 30/30/40-Spaltengewichte nicht inhaltsabhängig zusammenschiebt
- das bisher wie ein deaktiviertes achtes Eingabefeld wirkende Statusfeld im Kontakteditor ist nun eine rahmenlose Status-/Validierungszeile

- lokale Kontaktverwaltung als eigenes Modul (`contacts.c`, `contacts_import.c`, `gui_contacts.c`), getrennt von Gmail-, Netzwerk- und Account-Konfiguration
- neuer Menüpunkt **Datei → Kontakte...** ganz oben im Datei-Menü, danach Separator und die bestehenden Einträge
- Kontaktliste mit Vorname, Nachname und E-Mail-Adresse; Vor- und Nachname lassen sich per Spaltenkopf A–Z/Z–A sortieren
- Kontakte können mit Vorname, Nachname, Firma, E-Mail-Adresse, Telefon, Mobiltelefon und Website neu angelegt und bearbeitet werden
- Löschen verwendet den bestehenden AmiGmail-Bestätigungsdialog; Return/Enter bestätigt und Escape bricht eigene Kontaktfenster ab
- CSV-Import für Google-Contacts-ähnliche Exporte mit korrekter Behandlung von Quotes, Kommas und mehrzeiligen Feldern
- vCard/VCF-Import mit `N`, `ORG`, `EMAIL`, `TEL`, `URL`, gefalteten Zeilen sowie `itemN.TEL`/`X-ABLabel`-Mobiltelefonzuordnung
- importiert werden ausschließlich die sieben von AmiGmail unterstützten Kontaktfelder; weitere CSV-/vCard-Daten werden bewusst ignoriert
- Dublettenprüfung: gleiche E-Mail-Adresse (case-insensitiv), sonst Name plus übereinstimmende Telefonnummer bzw. bei Firmen Firma plus Telefonnummer; Dubletten werden übersprungen und nie automatisch überschrieben
- Google-CSV-Mehrfachwerte im Format ` ::: ` werden für die einwertigen AmiGmail-Felder auf den ersten Wert reduziert
- Kontaktdatei wird versioniert und getrennt als `ENVARC:AmiGmail/contacts.dat` gespeichert; Schreiben erfolgt über `.new` und einen rückrollbaren Replace-Schritt
- Verfassen/Antworten/Entwurf bearbeiten: `[...]` neben An/CC/BCC öffnet eine Mehrfachauswahl aus Kontakten mit E-Mail-Adresse und ergänzt die Empfänger ohne vorhandene Adressen zu überschreiben oder doppelt einzutragen

## 1.5 – 2026-08-15

- Programmversion auf 1.5 aktualisiert
- echtes ReAction/Workbench-Iconify stabilisiert; AmiGmail verwendet im iconifizierten Zustand das eingebettete `AmiGmail-Iconified.info`
- periodischer 5-Minuten-Abruf, Gmail-Netzwerk-Worker und `AmiGmailStatus` laufen auch iconifiziert weiter
- optionaler Benachrichtigungston fuer neue Mails mit Dateiauswahl fuer IFF/8SVX/WAV; 8SVX-Wiedergabe ueber `datatypes.library`/`sound.datatype` bestaetigt
- Konfigurations-Speichern blockiert die GUI nicht mehr; insbesondere kann ein veraltetes `timer.device`-Signal keinen neuen 5-Minuten-Timer mehr per `WaitIO()` festhalten
- Speichern lokaler Einstellungen startet den periodischen Timer nur neu, wenn sich die 5-Minuten-Option tatsaechlich geaendert hat
- bestehende Gmail-Verbindung bleibt bei rein lokalen Konfigurationsaenderungen erhalten

## v55 development fix (config-save freeze / stable iconify)

- Fixed the configuration-save freeze with periodic 5-minute fetching enabled.
  The timer event loop now calls `WaitIO()` only after `CheckIO()` confirms that
  the current timer request has actually completed; stale timer signals are
  cleared explicitly.
- Saving unrelated account preferences no longer restarts the five-minute timer.
  It is re-armed only when the `Periodic fetch (5 min.)` checkbox changes.
- Removed the experimental live Workbench AppIcon switching by unread-mail
  status. `window.class` documents `WINDOW_Icon` as the icon used for
  iconification, but does not guarantee replacement of an already visible
  AppIcon. AmiGmail therefore uses one stable embedded icon while iconified.
- Only the supplied `AmiGmail-Iconified.info` is embedded for the iconified
  Workbench state. The normal program icon is no longer duplicated inside the
  notification/iconify resource.
- Notification sound playback from v54 is retained unchanged.

# Änderungsprotokoll

### v54 development fix (Reconnect / dynamic AppIcon / sound playback)

- Account settings no longer synchronously stop/restart the network process. Local-only changes such as notification sound, fetch-on-start and the 5-minute toggle keep the existing Gmail connection alive.
- Network-relevant account changes are applied through a new asynchronous `AMG_NET_RECONFIGURE` worker command, so the ReAction GUI remains responsive while Gmail reconnects.
- Both supplied Workbench icons are embedded byte-for-byte: normal `AmiGmail.info` (6224 bytes) and `AmiGmail-Iconified.info` (6196 bytes).
- The iconified Workbench AppIcon now follows the same unread-Inbox state as `AmiGmailStatus`: normal icon at zero unread mails, new-mail icon at one or more unread mails.
- While iconified, a mail-status transition rebuilds the AppIcon through a hidden `WM_OPEN`/`WM_ICONIFY` cycle, avoiding a visible main-window flash.
- Notification sound playback now uses an explicit `DTST_FILE` sound DataType source, performs `DTM_PROCLAYOUT`, then calls `DTM_TRIGGER/STM_PLAY` through the classic `DoDTMethod()` varargs ABI.
- The account dialog consumes the sound completion signal while open and reports immediately whether the selected sound file could be loaded for preview.

### v52 development fix (Iconify / notification sound)

- Fixed the remaining classic GCC pointer-sign warning in the notification sound requester.
- Iconified Workbench state now prefers the supplied `AmiGmail-Iconified.info`.
- Notification sound playback now follows the classic `sound.datatype` signal-mask contract (`SDTA_SignalBit`).
- Playback objects are kept alive until the sound completion signal instead of interpreting the undocumented `DTM_TRIGGER` return value as an error.
- Selecting an IFF/8SVX/WAV file immediately previews it using the same playback path as real new-mail notifications.
- The sound requester continues to show `.iff`, `.8svx` and `.wav` case-insensitively.

## 1.4 – 2026-08-14

- echtes ReAction/Workbench-Iconify-Gadget ergänzt; AmiGmail wird als Workbench-AppIcon abgelegt und lässt sich per Doppelklick wiederherstellen, ohne den Prozess zu beenden
- periodischer 5-Minuten-Abruf, Netzwerk-Worker, Update-Prüfung und ENV-Mailstatus laufen auch im iconifizierten Zustand weiter; GUI-Modelle werden ohne offenen Intuition-Window-Pointer sicher aktualisiert
- `mailto:` während AmiGmail iconifiziert ist stellt das Hauptfenster zuerst wieder her und öffnet anschließend das Verfassen-Fenster wie gewohnt im Vordergrund
- optionale Konfiguration `Benachrichtigungston` / `Notification Sound` mit ASL-Dateiauswahl für IFF/8SVX ergänzt und mit den Kontoeinstellungen gespeichert
- neuer Mailton wird über `datatypes.library`/`sound.datatype` asynchron abgespielt: einmal pro Abruf mit tatsächlich neuen UIDs, auch im iconifizierten Zustand; bereits vorhandene Mails beim ersten Basisabruf bleiben stumm
- Fensterzustand korrigiert: window.class-Innenmasse werden getrennt von den aeusseren Intuition-Massen gespeichert, damit Breite und Hoehe pixelgenau wiederhergestellt werden
- Update-Anzeige im Header als kompakter zweizeiliger Status aufgebaut: transparenter Text `Version 1.4` plus `Aktuelle Version` / `Up to date`; bei neuem Release `Neues Update` / `new Update`
- deaktiviertes Ghost-/Punktmuster entfernt; der Status verwendet stattdessen `GA_ReadOnly` und einen dezenten duennen Buttonrahmen
- Update-Farbe von Vollrot auf ein dezentes Dunkelblau (`#003366`, soweit die Workbench-Palette dies abbilden kann) geaendert
- `AmiGmailStatus` meldet `Keine neue(n) Mail(s)` / `No new Mail` oder `Neue Mail(s) im Posteingang` / `New mail(s) in Inbox`; beim Beenden sowie nach einem Neustart bleibt `AmiGmail nicht aktiv` / `AmiGmail is not active` ueber ENVARC: erhalten
- einmalige, nicht blockierende GitHub-Release-Pruefung pro Programmstart mit numerischem Vergleich von Tags wie `v1.4` und `v1.10`
- Update-Download folgt dem Release-Schema `AmiGmail-vX.Y.lha` und speichert das Archiv unveraendert nach `RAM:`; Entpacken und Installation bleiben bewusst manuell
- globale Laufzeitvariable `AmiGmailStatus` signalisiert ungelesene Inbox-Mails und wird beim Lesen, Ungelesen-Markieren, Verschieben, Loeschen sowie periodischen Abruf nachgefuehrt
- Update-Check nutzt den vorhandenen Netzwerk-Worker und bleibt bei Netzwerk-/GitHub-Fehlern still, damit die GUI nicht blockiert wird

## 1.3 – 2026-08-14

- Programmversion auf 1.3 aktualisiert
- `mailto:`-Integration für Browser/andere Programme; Empfänger, CC/BCC, Betreff und Nachrichtentext können vorbelegt werden
- bereits laufendes AmiGmail übernimmt `mailto:`-Aufrufe über einen eigenen Exec-Port, ohne eine zweite IMAP-/SMTP-Sitzung zu starten
- IBrowse-Mailto-Aufrufe blockieren den Browser nicht; das Fenster „Neue Mail“ wird zuverlässig in den Vordergrund gebracht
- Escape bricht die Ordnerauswahl beim Verschieben einer Mail ab
- URL-Doppelklick in der Mailvorschau öffnet OpenURL außerhalb des TextEditor-Hooks und blockiert ReAction nicht
- Bestätigungsfenster für „Papierkorb leeren“ und „Spam leeren“ werden direkt mittig geöffnet
- große `gui.c` in klar getrennte Module für Ordner, Nachrichten, Vorschau, Fensteraufbau, Aktionen und Runtime/Eventloop zerlegt
- konservativer Compiler-Warning-Cleanup für Hook-/Tag-Pointer, Sprachvariable, Timer-Gerätename und Baumkoordinaten

## 1.2 – 2026-08-14

- Programmversion auf 1.2 aktualisiert
- Return/Enter bestätigt die eigenen ReAction-Requester; Escape bricht sie ab
- Im Ordner Entwürfe wird Antworten zu Bearbeiten/Edit
- bestehende Gmail-Entwürfe lassen sich mit Empfängern, CC/BCC, Betreff, Text und Anlagen weiterbearbeiten
- erneutes Speichern eines bearbeiteten Entwurfs ersetzt serverseitig den alten Draft; bei fehlgeschlagenem APPEND bleibt der alte Draft erhalten
- Senden eines bearbeiteten Entwurfs entfernt den alten Draft erst nach erfolgreichem SMTP-Versand
- Im Ordner Gesendet zeigt die zweite Datenspalte Empfänger/Recipient und verwendet To (mit Cc/Bcc-Fallback) statt des eigenen Absenders
- Multipart-Entwürfe mit Anlage und leerem Nachrichtentext werden korrekt als gültiger leerer Textteil erkannt und lassen sich wieder bearbeiten

## 1.1 – 2026-08-13

- Programmversion auf 1.1 aktualisiert
- Markierungsspalte: Rufzeichen im Spaltenkopf pixelgenau um 1 px nach links gesetzt
- About-Fenster zeigt AmiGmail 1.1
- gut auffindbare Binärkennung für AmiGmail Client 1.1 ergänzt

## 1.0 – 2026-08-13

- optionale periodische Inbox-Prüfung alle fünf Minuten; nach gesetzter UID-Basis werden nur neuere Nachrichten abgefragt
- „Periodischer Abruf (5 Min.)“ wird zusammen mit den Konto-Einstellungen gespeichert
- deutsche Oberfläche bei deutscher AmigaOS-Systemsprache, sonst vollständiger englischer Fallback
- Menüs, Hauptfenster, Konto-/Compose-Dialoge, Requester, Statusmeldungen und Protokollfehler sind Deutsch/Englisch lokalisiert
- eingebettetes 170×28-Banner wird direkt aus der 8-Farben-PNG als unkomprimiertes 3-Bitplane-ILBM erzeugt
- erste Developer-Preview für AmigaOS 3.2/68k mit ReAction-Grundlayout
- asynchroner Netzwerkprozess über Exec Message Ports
- AmiSSL-v5-TLS mit Zertifikats- und Hostnamenprüfung
- Gmail-IMAP-Anmeldung mit XOAUTH2 oder App-Passwort
- Labelermittlung, UID-basierter Seitenabruf und Gmail-IMAP-Erweiterungen
- MIME-, RFC-2047-, Base64-, Quoted-Printable- und Modified-UTF-7-Codecs
- SMTP-Antworten über direktes TLS mit Threading-Headern
- OAuth-2.0-PKCE-, Token-Exchange- und Refresh-Bausteine
- AES-256-GCM-Kontospeicher mit PBKDF2-Schlüsselableitung
- portable Hosttests und MSYS-/AmigaGCC-Makefile
- Nachrichten-Vorschau markiert ungelesene Mails automatisch als gelesen
- Schaltfläche „Un/Gelesen“ setzt den IMAP-Lesestatus für die Auswahl
- Antwortfenster mit vorbelegtem Empfänger, Betreff, Zitat und Thread-Headern
- Löschen nach Bestätigung verschiebt eine oder mehrere ausgewählte Mails in
  den Gmail-Papierkorb; Mehrfachauswahl erfolgt mit Umschalt/Shift
- kompakte native Auf-/Zuklapp-Symbole und bündige Texte im Labelbaum
- die Auswahlmarkierung bleibt beim automatischen Setzen auf „gelesen“ erhalten
- der Löschdialog öffnet mittig über dem AmiGmail-Hauptfenster
- zusätzliche Gmail-Sonderflags und lokalisierte Namen ordnen Spam und Entwürfe
  zuverlässig den Systemordnern zu
- „Markiert“ ist ausgeblendet; Statuszeilen der Nachrichtenliste stehen
  zentriert und umgebrochen in der Betreffspalte
- transparente, kleinere Chevron-Pfeile zeigen nach rechts beziehungsweise unten
- Start-Fix: `drawlist.image` ist nur noch optional und wird ohne
  Mindestversion geöffnet; fehlt die Klasse, verwendet ReAction automatisch
  seine eingebauten Hierarchie-Pfeile
- kompakter Löschdialog ohne unnötigen vertikalen Leerraum
- die angeklickte Nachrichtenzeile bleibt auch nach Vorschau und automatischer
  Gelesen-Markierung sichtbar ausgewählt
- gefüllte transparente 5×6-Pixel-Chevrons entsprechen dem gelieferten Vorbild
- IMAP hält eine serverseitig ermittelte Sonderordner-Tabelle und löst
  `\\Drafts` sowie `\\Spam` vor `SELECT` zum tatsächlichen Gmail-Mailboxnamen auf

### v51 - Sound requester and iconify icon polish
- Fixed the notification-sound ASL filter: ASLFR_AcceptPattern now receives a pattern tokenized with ParsePatternNoCase(), as required by asl.library.
- The sound requester now displays .iff, .8svx and .wav files case-insensitively.
- The iconified Workbench window now prefers PROGDIR:AmiGmail-Iconify.info, falls back to PROGDIR:AmiGmail.info, then to window.class' default icon.
- The titlebar iconify gadget itself remains the system-provided ReAction/window.class gadget.

### v53 - embedded iconify icon
- The supplied `AmiGmail-Iconified.info` is now embedded byte-for-byte in the AmiGmail executable.
- No separate iconify `.info` file is required in `PROGDIR:` or the release archive.
- AmiGmail materialises the embedded icon only briefly in `T:` so `icon.library` can build the native `DiskObject`, then deletes the temporary file immediately.
- The normal `AmiGmail.info` remains only as a safety fallback if the embedded icon cannot be loaded.

