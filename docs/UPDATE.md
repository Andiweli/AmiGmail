# AmiGmail 1.4: Update-Pruefung und Laufzeitstatus

## GitHub-Update-Pruefung

AmiGmail prueft pro Programmstart hoechstens einmal den neuesten stabilen
GitHub-Release. Die Versions-Tags muessen dem Schema `vX.Y` bzw. `vX.Y.Z`
entsprechen. Release-Archive werden nach dem fuer AmiGmail verwendeten Schema
`AmiGmail-vX.Y.lha` erwartet.

Beispiel:

- Tag: `v1.4`
- Archiv: `AmiGmail-v1.4.lha`

Ist der GitHub-Release numerisch neuer als die eingebaute Programmversion,
zeigt der kompakte Statusbereich rechts im Kopf zweizeilig die Programmversion und den Update-Status. Ohne neues Release steht dort:

- `v1.4`
- Deutsch: `Aktuelle Version`
- Englisch: `Up to date`

Ist eine neuere Version verfuegbar, wechselt die zweite Zeile auf einen dezenten dunkelblauen Button:

- Deutsch: `Neues Update`
- Englisch: `new Update`

Ein Klick laedt das LHA unveraendert nach `RAM:`. Installation und Entpacken
werden absichtlich nicht automatisiert.

Fehler bei der automatischen Update-Pruefung bleiben still. Ein Fehler beim
vom Benutzer gestarteten Download wird dagegen in der AmiGmail-Statuszeile
angezeigt.

### Testen vor der Veroeffentlichung von 1.4

Solange GitHub noch `v1.3` als neuesten Release liefert, zeigt eine echte
AmiGmail-1.4-Binaerdatei korrekterweise keinen Update-Hinweis. Fuer den
Entwicklertest kann temporaer gesetzt werden:

    SetEnv AmiGmailUpdateTest 1

Beim naechsten Programmstart wird der aktuell von GitHub gelieferte Release
als testweise verfuegbar behandelt. Ein Klick sollte derzeit beispielsweise
`RAM:AmiGmail-v1.3.lha` erzeugen. Nach dem Test kann die Funktion mit

    SetEnv AmiGmailUpdateTest 0

wieder deaktiviert werden.

`AmiGmailUpdateTest` ist ausschliesslich ein Testschalter und veraendert nicht,
welcher Release oder welche Datei von GitHub geliefert wird.

## ENV-Status fuer ungelesene Inbox-Mails

AmiGmail pflegt waehrend der Laufzeit die globale ENV-Variable
`AmiGmailStatus`.

Bei mindestens einer ungelesenen Mail im Posteingang lautet ihr Inhalt:

- Deutsch: `Neue Mail(s) im Posteingang`
- Englisch: `New mail(s) in Inbox`

Sobald der Inbox-Status bekannt ist und keine ungelesenen Mails vorhanden sind:

- Deutsch: `Posteingang leer`
- Englisch: `Inbox empty`

Solange der Inbox-Status nach dem Programmstart noch nicht bekannt ist, bleibt
die Variable entfernt. Sie wird ebenfalls beim normalen Programmende geloescht,
damit kein veralteter Status aus einer frueheren Sitzung stehen bleibt. Es
erfolgt kein Schreiben nach `ENVARC:`.

## Hauptfenster

Beim normalen Beenden werden Position sowie innere und aeussere Groesse des
Hauptfensters in `ENVARC:AmiGmail/window.state` gespeichert. window.class
erwartet beim Wiederherstellen Innenmasse; deshalb werden die Innenmasse fuer
die exakte Fenstergroesse und die Aussenmasse nur fuer die Bildschirmbegrenzung
verwendet. Das Stateformat wurde auf `AMIGMAIL-WINDOW-2` angehoben; eine alte
v46-Datei wird einmalig ignoriert und nach dem naechsten Beenden neu erzeugt.
