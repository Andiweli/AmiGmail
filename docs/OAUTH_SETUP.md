# OAuth-2.0-Einrichtung für AmiGmail

## Google-Konfiguration

1. In der Google Cloud Console ein Projekt auswählen oder anlegen.
2. Die Gmail API aktivieren.
3. Den OAuth-Zustimmungsbildschirm konfigurieren. Während des Testmodus muss
   das verwendete Gmail-Konto als Testnutzer eingetragen sein.
4. Einen OAuth-Client vom Typ **Desktopanwendung** anlegen.
5. Die Client-ID in `include/oauth_client_config.h` eintragen.

AmiGmail benötigt für IMAP und SMTP den Scope:

```text
https://mail.google.com/
```

## Vorgesehener Ablauf

1. AmiGmail erzeugt einen kryptografischen PKCE-Verifier, die S256-Challenge
   und einen State-Wert.
2. Der Nutzer öffnet die von `amg_oauth_build_authorize_url` erzeugte URL in
   einem Browser.
3. Google leitet den Browser zu einem lokalen Loopback-Callback um.
4. AmiGmail vergleicht den State-Wert und tauscht den Code zusammen mit dem
   PKCE-Verifier gegen Access- und Refresh Token.
5. Das Access Token wird nur im RAM gehalten und für IMAP/SMTP als XOAUTH2
   verwendet.
6. Das Refresh Token wird ausschließlich innerhalb des verschlüsselten
   Kontospeichers abgelegt.

Die Low-Level-Schritte 1, 2, 4, 5 und 6 sind als C-Module vorhanden. Der lokale
HTTP-Callback, das Öffnen der URL und die ReAction-Führung fehlen noch.

## Redirect URI

Für einen installierten Client ist eine Loopback-Adresse vorgesehen, zum
Beispiel:

```text
http://127.0.0.1:53682/oauth2/callback
```

Die konkrete URI muss im `AmgOAuthConfig` exakt mit der beim Autorisieren und
beim Tokenaustausch verwendeten URI übereinstimmen. Port und Callback dürfen
erst nach erfolgreichem Binden festgelegt werden.

## Sicherheitsregeln

- niemals das normale Google-Passwort abfragen
- Access Tokens nicht auf Platte speichern
- Refresh Token nur verschlüsselt speichern
- `state` vor dem Codeaustausch vergleichen
- PKCE-Verifier bis zum Abschluss nur im RAM halten
- Client-Secret in einer Desktopanwendung nicht als echtes Geheimnis behandeln
- OAuth-Fehlertexte nicht zusammen mit Tokens protokollieren

## Alternative App-Passwort

Wenn OAuth für einen privaten Build nicht eingerichtet wird, kann ein
Google-App-Passwort genutzt werden. Dafür ist die Bestätigung in zwei Schritten
am Google-Konto erforderlich. Das 16-stellige App-Passwort wird bei IMAP und
SMTP per `AUTHENTICATE PLAIN` beziehungsweise `AUTH PLAIN` innerhalb der
verifizierten TLS-Verbindung übertragen.

