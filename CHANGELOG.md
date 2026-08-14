# Änderungsprotokoll

## 1.4 – 2026-08-14

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
