# AmiGmail als `mailto:`-Handler

AmiGmail 1.3 kann beim Programmstart eine vollständige `mailto:`-URI übernehmen.
Unterstützt werden Empfänger, `to`, `cc`, `bcc`, `subject` und `body`, inklusive
Prozentkodierung.

Direkter Test aus der Shell:

```text
AmiGmail "mailto:test@example.com?subject=Hallo&body=Test"
```

AmiGmail wertet auf AmigaOS nicht nur `argc/argv`, sondern zusätzlich die rohe
AmigaDOS-Argumentzeile über `GetArgStr()` aus. Das ist wichtig für externe
Browser-Kommandos, deren Argumentübergabe nicht zwingend der üblichen
C-Runtime-Aufteilung entspricht.

## Asynchroner Browser-Start

Ein Browser darf nicht blockieren, solange AmiGmail geöffnet bleibt. Deshalb
arbeitet ein `mailto:`-Start in zwei Stufen:

1. Läuft AmiGmail bereits, wird die vollständige URI sofort über den
   öffentlichen Exec-Port `AMIGMAIL.MAILTO` an diese Instanz übergeben und der
   neue Prozess beendet sich.
2. Läuft AmiGmail noch nicht, schreibt der kurze Handler-Prozess die URI nach
   `T:`, startet die eigentliche AmiGmail-Instanz über `SystemTags()` mit
   `SYS_Asynch=TRUE` und beendet sich anschließend sofort. Die neue Instanz
   liest die URI, löscht die temporäre Übergabedatei und öffnet **Neue Mail**.

Damit bleibt der aufrufende Browser auch beim allerersten `mailto:`-Klick
bedienbar und es entsteht keine zweite IMAP-/SMTP-Sitzung, wenn AmiGmail schon
läuft.

## IBrowse direkt

In IBrowse:

- **Preferences > Network > E-mail & Telnet**
- **Type (Mailto:)**: `External`
- **Action (Mailto:)**: `Command`
- **Program**: die AmiGmail-Programmdatei
- **Parameters**:

```text
mailto:%h
```

`%h` ist laut IBrowse die Ziel-Mailadresse des angeklickten `mailto:`-Links.
Für den Grundtest bitte zunächst ausschließlich `mailto:%h` verwenden.

Wenn dieser Test funktioniert, kann der Betreff ergänzt werden:

```text
"mailto:%h?subject=%s"
```

Die Anführungszeichen sind sinnvoll, weil ein Betreff Leerzeichen enthalten
kann. IBrowse stellt komplexere Inhalte des Links zusätzlich über `%f` als
temporäre Datei bereit. Die native `%f`-Übernahme ist nicht Bestandteil dieses
Grundfixes.

## OpenURL

AmiGmail kann auch über OpenURL als Mailprogramm verwendet werden. Der
AmiGmail-interne Port `AMIGMAIL.MAILTO` ist **kein ARexx-Port** und darf daher
nicht als ARexx-Port in OpenURL eingetragen werden.

## Verhalten

- Neue Instanz: Konto wird wie üblich entsperrt, danach öffnet sich direkt
  **Neue Mail** mit den Daten aus dem Link.
- Bereits laufende Instanz: Link wird an diese Instanz weitergereicht.
- Browser-Aufruf: der aufrufende Browser wird nicht bis zum Schließen von
  AmiGmail blockiert.
- Bereits geöffnetes modales AmiGmail-Fenster: Der Link bleibt im Exec-Port
  gepuffert und wird verarbeitet, sobald der laufende Dialog beendet ist.
- Abgebrochene Kontoentsperrung: Es wird kein Verfassen-Fenster geöffnet.
- Ungültige Prozentkodierung oder eingebettete NUL-Bytes werden verworfen.
- Zeilenumbrüche in Empfänger-/Betrefffeldern werden neutralisiert; im Body
  bleiben Zeilenumbrüche erhalten.
