# AmiGmail Changelog

## 1.8 – 2026-08-18

- Program version updated to 1.8
- Reply button implemented as a compact split button: the main action remains **Reply**, while the dropdown offers **Reply All** and **Forward**
- **Reply All** places the sender in To and additional recipients in CC; the user's own Gmail address and duplicate recipients are removed case-insensitively
- **Forward** opens a new message without prefilled recipients, creates an appropriate `Fwd:` subject, and includes the original message text and existing MIME attachments within the existing size limits
- New **Signature** management under **Edit**; signatures are stored locally in `ENVARC:AmiGmail/signature.txt` and are inserted automatically when composing new messages, replying, replying to all, and forwarding
- Compact signature editor with a TextEditor field sized for approximately 60 characters × 5 lines; Return/Enter or the buttons save, Escape cancels
- Menu structure revised: **Contact management** is now the first entry under **Edit**, followed by **Signature**, then a separator and the existing actions for emptying Trash and Spam
- Contacts window renamed to **AmiGmail - Contact management**
- Message column widths intentionally remain at their defined defaults; the experimentally tested persistent column-width storage was removed completely
- New centered **ReAction splash screen** during startup using the embedded AmiGmail header graphic, current version number, localized AmigaOS 3.2 client line, and copyright notice
- Splash screen uses the normal application/screen font, left-aligned text, and a subtle frame; required ReAction classes are opened with separate temporary library references without corrupting the library bases used later by the main GUI
- Splash screen and About window now show the copyright as **© Andreas Stürmer**
- About window and splash screen use the central `AMIGMAIL_VERSION` value so the displayed version number automatically follows future releases

## 1.7 – 2026-08-17

- Program version updated to 1.7
- Fixed the notification sound for the first new mail after a restart: a valid stored Inbox baseline with `UID 0` is now correctly distinguished from “no baseline available yet”
- This also reliably handles the case where AmiGmail is closed with an empty Inbox and the first new message arrives while the program is not running
- Existing UID/UIDVALIDITY logic for normal startup fetches and already known messages remains unchanged
- Recommended Workbench stack for release use increased to **100000 bytes**; values below approximately 65000 bytes should be avoided
- Version identifiers in the program, Makefile, and binary version string updated to 1.7

## 1.6 – 2026-08-15

- Program version updated to 1.6; contacts/address book functionality finalized as the official release state
- v58 focus/TAB fix: the contact editor activates the first string gadget through `ActivateLayoutGadget()` inside the ReAction layout; TAB now cycles reliably through all seven contact fields
- v59 UI polish: contact lists now use the same compact row height as mail/label lists (`Screen font height + 2`), so descenders such as `g`, `p`, `q`, and `y` remain fully visible
- First name and last name now use identical column widths; `LISTBROWSER_AutoFit` was removed from contact lists so ReAction no longer compresses the intended 30/30/40 column weights based on content
- The status field in the contact editor, which previously looked like a disabled eighth input field, is now a borderless status/validation line
- Local contact management implemented as separate modules (`contacts.c`, `contacts_import.c`, `gui_contacts.c`), isolated from Gmail, networking, and account configuration
- New **File → Contacts...** menu item added at the top of the File menu, followed by a separator and the existing entries
- Contact list with first name, last name, and email address; first and last name can be sorted A–Z/Z–A via the column headers
- Contacts can be created and edited with first name, last name, company, email address, phone, mobile phone, and website
- Deletion uses the existing AmiGmail confirmation dialog; Return/Enter confirms and Escape cancels in dedicated contact windows
- CSV import for Google Contacts-like exports with correct handling of quotes, commas, and multiline fields
- vCard/VCF import with `N`, `ORG`, `EMAIL`, `TEL`, `URL`, folded lines, and `itemN.TEL`/`X-ABLabel` mobile-phone mapping
- Only the seven contact fields supported by AmiGmail are imported; additional CSV/vCard data is intentionally ignored
- Duplicate detection: same email address (case-insensitive), otherwise name plus matching phone number, or for companies company plus phone number; duplicates are skipped and never overwritten automatically
- Google CSV multi-value fields in the ` ::: ` format are reduced to the first value for AmiGmail's single-value fields
- Contact file is versioned and stored separately as `ENVARC:AmiGmail/contacts.dat`; writes use `.new` plus a rollback-capable replacement step
- Compose/Reply/Edit Draft: `[...]` next to To/CC/BCC opens a multi-selection list of contacts with email addresses and adds recipients without overwriting existing addresses or creating duplicates

## 1.5 – 2026-08-15

- Program version updated to 1.5
- Real ReAction/Workbench iconify behavior stabilized; AmiGmail uses the embedded `AmiGmail-Iconified.info` while iconified
- Periodic five-minute fetching, the Gmail network worker, and `AmiGmailStatus` continue running while the application is iconified
- Optional new-mail notification sound with file selection for IFF/8SVX/WAV; 8SVX playback via `datatypes.library`/`sound.datatype` confirmed
- Saving configuration no longer blocks the GUI; in particular, a stale `timer.device` signal can no longer trap a new five-minute timer in `WaitIO()`
- Saving local preferences restarts the periodic timer only when the five-minute option actually changes
- Existing Gmail connections remain active when only local configuration values are changed

## v55 development fix (config-save freeze / stable iconify)

- Fixed the configuration-save freeze with periodic five-minute fetching enabled.
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

# Change log

### v54 development fix (Reconnect / dynamic AppIcon / sound playback)

- Account settings no longer synchronously stop/restart the network process. Local-only changes such as notification sound, fetch-on-start and the five-minute toggle keep the existing Gmail connection alive.
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

- Added real ReAction/Workbench iconify gadget; AmiGmail becomes a Workbench AppIcon and can be restored by double-clicking it without terminating the process
- Periodic five-minute fetching, network worker, update check, and ENV mail status continue running while iconified; GUI models are updated safely without an open Intuition window pointer
- `mailto:` calls received while AmiGmail is iconified restore the main window first and then open the Compose window in the foreground as usual
- Added optional **Notification Sound** configuration with ASL file selection for IFF/8SVX and persistent account storage
- New-mail sound is played asynchronously through `datatypes.library`/`sound.datatype`: once per fetch containing genuinely new UIDs, including while iconified; existing messages found during the first baseline fetch remain silent
- Fixed window-state persistence: window.class inner dimensions are stored separately from the outer Intuition dimensions so width and height are restored pixel-perfectly
- Update display in the header reworked as a compact two-line status: transparent `Version 1.4` text plus `Up to date`; when a newer release exists, `new Update` is shown
- Removed the disabled ghost/dither pattern; the status now uses `GA_ReadOnly` and a subtle thin button frame
- Update color changed from bright red to a subtle dark blue (`#003366`, as closely as the Workbench palette allows)
- `AmiGmailStatus` reports `No new Mail` or `New mail(s) in Inbox`; after quitting and after restart, `AmiGmail is not active` remains stored via ENVARC:
- Added a one-time non-blocking GitHub release check per program start with numeric comparison of tags such as `v1.4` and `v1.10`
- Update download follows the release naming scheme `AmiGmail-vX.Y.lha` and saves the archive unchanged to `RAM:`; extraction and installation intentionally remain manual
- Global runtime variable `AmiGmailStatus` reports unread Inbox mail and is updated when reading, marking unread, moving, deleting, and during periodic fetching
- Update checking uses the existing network worker and stays silent on network/GitHub failures so the GUI is never blocked

## 1.3 – 2026-08-14

- Program version updated to 1.3
- Added `mailto:` integration for browsers and other applications; recipient, CC/BCC, subject, and message body can be prefilled
- An already running AmiGmail instance accepts `mailto:` calls through its own Exec port without starting a second IMAP/SMTP session
- IBrowse mailto calls no longer block the browser; the New Mail window is reliably brought to the foreground
- Escape cancels folder selection when moving a message
- Double-clicking a URL in the mail preview opens OpenURL outside the TextEditor hook and no longer blocks ReAction
- Confirmation windows for Empty Trash and Empty Spam open centered immediately
- Large `gui.c` split into clearly separated modules for folders, messages, preview, window construction, actions, and runtime/event loop
- Conservative compiler-warning cleanup for hook/tag pointers, language variable, timer device name, and tree coordinates

## 1.2 – 2026-08-14

- Program version updated to 1.2
- Return/Enter confirms AmiGmail's own ReAction requesters; Escape cancels them
- In the Drafts folder, Reply changes to Edit
- Existing Gmail drafts can be reopened with recipients, CC/BCC, subject, text, and attachments restored
- Saving an edited draft again replaces the old server-side draft; if APPEND fails, the old draft is retained
- Sending an edited draft removes the old draft only after successful SMTP delivery
- In Sent, the second data column displays Recipient and uses To, with Cc/Bcc fallback, instead of the user's own sender address
- Multipart drafts with an attachment and an empty message body are correctly recognized as having a valid empty text part and can be edited again

## 1.1 – 2026-08-13

- Program version updated to 1.1
- Flag column: exclamation mark in the column header moved exactly 1 px to the left
- About window displays AmiGmail 1.1
- Added an easily discoverable binary identifier for AmiGmail Client 1.1

## 1.0 – 2026-08-13

- Optional periodic Inbox check every five minutes; after establishing the UID baseline, only newer messages are requested
- `Periodic fetch (5 min.)` is stored together with the account settings
- German interface when the AmigaOS system language is German, otherwise a complete English fallback
- Menus, main window, account/compose dialogs, requesters, status messages, and protocol errors are localized in German and English
- Embedded 170×28 banner generated directly from the 8-color PNG as an uncompressed 3-bitplane ILBM
- First developer preview for AmigaOS 3.2/68k with basic ReAction layout
- Asynchronous network process using Exec Message Ports
- AmiSSL v5 TLS with certificate and hostname verification
- Gmail IMAP login using XOAUTH2 or app password
- Label discovery, UID-based paged fetching, and Gmail IMAP extensions
- MIME, RFC 2047, Base64, Quoted-Printable, and Modified UTF-7 codecs
- SMTP replies over direct TLS with threading headers
- OAuth 2.0 PKCE, token exchange, and refresh building blocks
- AES-256-GCM account storage with PBKDF2 key derivation
- Portable host tests and MSYS/AmigaGCC Makefile
- Message preview automatically marks unread mail as read
- `Read/Unread` button toggles the IMAP read state for the selection
- Reply window with prefilled recipient, subject, quote, and thread headers
- Delete after confirmation moves one or more selected messages to Gmail Trash; multi-selection uses Shift
- Compact native expand/collapse symbols and aligned text in the label tree
- Selection highlight remains intact when a message is automatically marked as read
- Delete dialog opens centered over the AmiGmail main window
- Additional Gmail special flags and localized names reliably map Spam and Drafts to the corresponding system folders
- Starred is hidden; status rows in the message list are centered and wrapped in the Subject column
- Transparent smaller chevrons point right or down
- Startup fix: `drawlist.image` is now optional and is opened without a minimum version; if the class is missing, ReAction automatically falls back to its built-in hierarchy arrows
- Compact delete dialog without unnecessary vertical whitespace
- The clicked message row remains visibly selected after preview and automatic marking as read
- Filled transparent 5×6-pixel chevrons match the supplied reference
- IMAP keeps a server-detected special-folder table and resolves `\Drafts` and `\Spam` to the actual Gmail mailbox name before `SELECT`

### v51 - Sound requester and iconify icon polish

- Fixed the notification-sound ASL filter: `ASLFR_AcceptPattern` now receives a pattern tokenized with `ParsePatternNoCase()`, as required by asl.library.
- The sound requester now displays `.iff`, `.8svx` and `.wav` files case-insensitively.
- The iconified Workbench window now prefers `PROGDIR:AmiGmail-Iconify.info`, falls back to `PROGDIR:AmiGmail.info`, then to window.class' default icon.
- The titlebar iconify gadget itself remains the system-provided ReAction/window.class gadget.

### v53 - Embedded iconify icon

- The supplied `AmiGmail-Iconified.info` is now embedded byte-for-byte in the AmiGmail executable.
- No separate iconify `.info` file is required in `PROGDIR:` or the release archive.
- AmiGmail materializes the embedded icon only briefly in `T:` so `icon.library` can build the native `DiskObject`, then deletes the temporary file immediately.
- The normal `AmiGmail.info` remains only as a safety fallback if the embedded icon cannot be loaded.
