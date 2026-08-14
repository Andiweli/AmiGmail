# ![AmiGmail icon](https://github.com/Andiweli/AmiGmail/blob/main/images/amigmail-icon.jpg) AmiGmail 1.2

**AmiGmail** is an open-source, single-account IMAP e-mail client for **AmigaOS 3.2+ (68k)**, built with **ReAction**, **AmiSSL**, IMAP and SMTP.

Despite its name, AmiGmail is not limited to Gmail. Gmail is the primary target and receives special handling for its system folders and labels, but standard IMAP/SMTP accounts can also be configured.

The application is designed as a lightweight native Amiga mail client: messages stay on the mail server and are accessed through IMAP rather than being stored in a local offline mail database.

![AmiGmail main window](https://github.com/Andiweli/AmiGmail/blob/main/images/amigmail.jpg)

## 📧 Features

- Single-account IMAP/SMTP mail client
- Login using e-mail address and password or app password
- Configurable IMAP and SMTP servers and ports
- Secure TLS connections through AmiSSL
- Gmail-compatible default system-folder mapping for Inbox, Sent, Drafts, Trash and All Mail
- System folders can be reassigned when a provider uses different mailbox names
- Nested IMAP folders/labels shown as an expandable folder tree
- Folder expansion state is remembered
- Optional automatic Inbox fetch when AmiGmail starts
- Optional automatic Inbox refresh every 5 minutes
- Compose new mail and reply to messages
- Move messages between folders and back to the Inbox
- Delete messages and empty the Trash folder
- Mark messages as read or unread
- Multi-selection for selected message operations using Shift
- Open and continue saved drafts in the compose window
- Saving an edited draft replaces the previous server-side draft safely
- Send attachments up to 10 MB
- Save attachments from received messages
- Sort the message list by sender, subject, date or message size
- MIME, Base64, Quoted-Printable and RFC 2047 handling
- Plain-text message display with a simple HTML-to-text fallback
- RFC 5322 line handling, SMTP dot-stuffing, `In-Reply-To` and `References`
- Modified UTF-7 support for IMAP mailbox names
- Gmail IMAP extensions including `X-GM-MSGID`, `X-GM-THRID` and `X-GM-LABELS`
- Separate network process using Exec Message Ports so network activity does not block the GUI task
- German and English user interface

## 🖥️ User Interface

The ReAction main window is divided into three main areas:

1. **Left:** system folders and the expandable IMAP folder/label tree
2. **Top right:** message list with sender, subject, date and size
3. **Bottom right:** read-only message preview

The toolbar provides the most frequently used actions such as **New Mail**, **Fetch**, **Reply**, **Delete**, **Move** and **Read/Unread**. In the Drafts folder, the corresponding action opens the selected draft for editing.

A status bar at the bottom displays connection and mail-operation information.

The user interface automatically follows the AmigaOS system language:

- **German** when AmigaOS is configured for German
- **English** for all other system languages

The language is selected at program startup through the AmigaOS `LanguageName` system variable.

## 📁 IMAP Folders and Gmail Labels

AmiGmail displays standard mailboxes and user-created folders in the left pane. Nested folders can be expanded and collapsed, and their current view state is restored when AmiGmail is started again.

For Gmail accounts, AmiGmail understands Gmail's special-use folders and label model. For other IMAP providers, the standard folders can be mapped manually if the provider uses different mailbox names.

Messages can be moved between folders without creating a local copy of the mailbox database.

## ✉️ Drafts

Draft messages are stored on the IMAP server.

Existing drafts can be reopened in the normal compose window, including recipients, body text, attachments and reply references where available. Saving the message again replaces the previous draft. When a draft is successfully sent, the old server-side draft is removed only after successful SMTP delivery.

## 📎 Attachments

AmiGmail can add file attachments to outgoing messages and save attachments from received messages.

The current outgoing attachment limit is **10 MB**.

## 🔐 Configuration and Security

Account data is protected by a **mandatory master password**.

Sensitive account information is stored using:

- PBKDF2-HMAC-SHA256 key derivation
- 100,000 PBKDF2 iterations
- AES-256-GCM authenticated encryption
- temporary `.new` file creation followed by rename when writing the account file
- explicit clearing of sensitive temporary buffers where practical

The master password itself is not stored. After a computer restart, it must be entered again before the saved account can be unlocked.

The account configuration contains the e-mail address, server settings, ports, authentication data and optional automatic-fetch settings.

## 🌐 Gmail and Other Mail Providers

AmiGmail was designed with Gmail compatibility in mind, but the account settings allow custom IMAP and SMTP hosts and ports for other providers.

Typical Gmail settings are:

```text
IMAP server: imap.gmail.com
IMAP port:   993
SMTP server: smtp.gmail.com
SMTP port:   465 or 587, depending on the selected TLS mode
```

For Gmail, an **app password** can be used with accounts that have two-step verification enabled. The app password is used instead of the normal Google account password.

Other IMAP providers may require different server names, ports, folder mappings or authentication settings.

## ⚙️ Requirements

### Target system

- AmigaOS 3.2 or newer
- 68020 CPU or newer
- TCP/IP stack providing `bsdsocket.library` V4
- AmiSSL v5 with a valid `AmiSSL:` assign and current CA bundle
- ReAction classes supplied with AmigaOS 3.2
- Correct system date and time for TLS certificate validation

AmiGmail has been developed for classic-style AmigaOS systems as well as accelerated setups. The GUI is intended to remain usable from HiRes Interlace configurations through higher-resolution P96 screens.

## 🛠️ Building AmiGmail

### Build host

A typical cross-build environment requires:

- Windows with MSYS2/MSYS, or another POSIX-compatible host
- `make`
- `zip`
- an m68k AmigaOS cross compiler such as `m68k-amigaos-gcc`
- AmigaOS 3.2 NDK/ReAction includes
- AmiSSL v5 SDK for m68k

### Basic build

The Makefile derives `AMIGA_PREFIX` from the `m68k-amigaos-gcc` found in `PATH` whenever possible.

A normal build is therefore:

```sh
make clean
make
```

The resulting executable is:

```text
bin/AmiGmail
```

The default compiler settings use `-m68020` and `-msoft-float`.

### Explicit SDK paths

If the SDKs are installed in custom locations, the paths can be supplied explicitly:

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

`AMISSL_SDK` must point to the AmiSSL `Developer` directory containing `include/` and `lib/`.

The exact directory layout depends on the AmigaGCC distribution. What matters is that the build can find the NDK/ReAction headers, the AmiSSL/OpenSSL headers and the required m68k libraries.

## 🧪 Host Tests

Portable modules can be tested without a complete Amiga SDK installation:

```sh
make host-test
make host-check
```

`host-test` covers areas such as:

- Base64
- Quoted-Printable
- Modified UTF-7
- fragmented IMAP responses and literals
- MIME and RFC 2047
- SMTP dot-stuffing
- OAuth PKCE helpers
- SHA-256
- account validation

`host-check` compiles the source tree with strict warnings while Amiga-specific code paths are replaced by host stubs. The resulting host binary is a build-check target and is not intended to run as a Linux mail client.

## 🧩 Architecture

The codebase is separated into protocol, GUI, security and asynchronous networking components.

| Area | Main modules | Purpose |
|---|---|---|
| Application / GUI | `app.c`, `gui.c`, `i18n.c` | lifecycle, ReAction GUI and language handling |
| Networking | `network_task.c` | asynchronous network process and Exec Message Ports |
| IMAP | `imap.c`, `imap_parser.c` | authentication, folders, labels, UIDs and literals |
| SMTP | `smtp.c` | message delivery and replies |
| Message handling | `mime.c`, `codec.c`, `charset.c` | MIME, transfer encodings and text conversion |
| Security | `tls.c`, `oauth.c`, `crypto.c`, `storage.c` | AmiSSL, OAuth helpers, cryptography and encrypted account storage |
| Tests | `tests/test_main.c` | portable protocol and codec tests |

Further technical information is available in:

- [Architecture](docs/ARCHITECTURE.md)
- [OAuth setup](docs/OAUTH_SETUP.md)

## 🔑 OAuth 2.0

The source tree contains OAuth 2.0 helper functions for Authorization Code + PKCE and token refresh.

A complete ReAction-based OAuth login wizard with a local loopback callback is not currently part of the normal user workflow. For Gmail, using an app password is therefore the straightforward configuration method at present.

A desktop OAuth client secret should never be treated as a protected secret when embedded in a publicly distributed 68k executable.

## ⚠️ Current Limitations

- Single account only
- No local offline mail cache
- No full-text search database
- Message composition is plain text rather than an HTML editor
- MIME parsing intentionally focuses on the message formats needed for normal mail usage rather than implementing every possible deeply nested MIME structure
- OAuth helper code exists, but the full GUI OAuth login flow is not yet integrated
- User interface languages are limited to German and English

## 🖼️ Configuration

![AmiGmail configuration](https://github.com/Andiweli/AmiGmail/blob/main/images/amigmail-config.jpg)

## 🔗 References

- [Gmail IMAP, POP and SMTP](https://developers.google.com/workspace/gmail/imap/imap-smtp)
- [Gmail IMAP extensions and labels](https://developers.google.com/workspace/gmail/imap/imap-extensions)
- [Google OAuth 2.0 for installed applications](https://developers.google.com/identity/protocols/oauth2/native-app)
- [AmiSSL project and SDK information](https://github.com/jens-maus/amissl)

## ⚖️ Legal

AmiGmail is an independent, non-commercial hobby project and is **not affiliated with, developed by, supported by or sponsored by Google LLC**.

**Gmail** is a trademark of Google LLC. **Amiga** and **AmigaOS** are trademarks of their respective owners. AmiSSL and other third-party components remain subject to their respective licenses.

Copyright © Andreas 'Andiweli' Stürmer.
