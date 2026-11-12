# Transports: new ways to send, in every application at once

**Status: a proposal; two transports built (2026-11-12) - Files, "Save a
copy to a folder", and Internet mail (State).**

## What Newton has

The Action button routes anything with a suitable format through any
registered transport (`RegTransport`): print, fax, beam, mail. Items wait
in the Out Box until the transport can send; received items arrive in the
In Box. The host already adds transports this way: text messages
(`comms/host/HostText.ns`) and the telephone
(`docs/host-tdeck-pro-phone.md`); printing goes to network printers by IPP
(`docs/print/README.md`).

## The aim

Each transport below is one host-loaded NewtonScript file plus, where
needed, a host service. Once registered it appears in the Action menu of
every application, the ROM's and third parties', with no change to them.

## The transports

| Transport | Sends | Needs |
|---|---|---|
| **Internet mail** | text, and frames as attachments, by SMTP; receives by IMAP into the In Box | TLS for endpoints (`docs/modernization-plan.md`, Phase 1); account settings in Internet Setup's style; passwords in the host's key store, never in a soup |
| **Mesh** | text to a MeshCore contact or channel | the mesh engine (`docs/host-tdeck-pro-mesh.md`), now an application of its own |
| **Files** ("Save a copy to a folder"; `files.md` has files as entries) | anything, written to a directory of a file store: text as `.txt`, a note's ink and shapes as PDF or PNG, a frame as NSOF, a picture as its original file | a host native for the directory; the PostScript and page drawing the print path already has |
| **Another Newton** | any frame, to a paired device over the network | the pairing of `sync.md`; the beam transport's receiving side as the model |
| **Share to the host** | text or a picture to the host's clipboard or share mechanism | per host; nothing where the Newton is the whole system |

Order: Files first (no network, useful at once, exercises the formats),
then mail, then the rest.

## Rules

- A transport declares the data types it takes, as the ROM's do, so the
  Action menu offers it only where it applies.
- Sending is a permission (`permissions.md`). A package that routes without
  the user's tap needs the `send` grant.
- Everything queues in the Out Box when the carrier is down and goes when
  it returns; a failure stays in the Out Box with its reason, as a failed
  fax does.
- Received items go to the In Box with the ROM's "Put away" unless the
  transport has an application of its own, as text messages have.

## Tests

For each, as `host.NewtonTextMessage` does for texts: an item routed from
the Notepad and from Names, queued with the carrier down, sent when it
comes up, and one received and put away. Mail against a local test server
(`tools/host/` has the HTTP one as a model); Files against a temporary
directory.

## Owner decisions

1. Which transports, and in what order.
2. Mail: one account or several; whether received mail goes to the In Box
   or to a mail application like Messages. **Decided (2026-11-12):** one
   main account, signed in by default, with secondary accounts beside it
   that the user or any third-party package can add; received mail goes
   to the In Box.

## State (2026-11-12, branch `modern`)

Built: the **Files** transport, registered while the Host panel's "Files"
is on (`NEWTON_FILES`; `src/host/HostFiles.ns`'s `transport`, made as
`comms/host/HostText.ns` is, from `protoTransport` @389 and
`protoFullRouteSlip` @655). Every application whose data has a `'text` or
`'frame` route format - the ROM's Notepad and Names among them - now has
"Files" in its Action button's menu beside Print, Fax and Beam, with no
change to the application.

- The slip adds two lines: **Folder** (a pop-up of the Files folder's
  standard folders - Documents, Downloads, Pictures, Music, Received;
  Documents by default; the item's `saveFolder`) and **As** (Text or
  Newton data; the item's `saveAs`). The slip's button is "Save to Files".
- The item's title names the file, less what no file system takes (`/ \ :
  * ? " < > |`, control characters, trailing full stops); "Untitled" when
  nothing is left; a name taken gets " 2", " 3", ...
- As text: the route format's `TextScript`, written as `<title>.txt`
  (UTF-8, LF). As Newton data: the item's data flattened with
  `Translate(data, 'flattener, nil, nil)` - the Newton Streamed Object
  Format Beam sends - and written as `<title>.nsof` (native
  `HostFilesWriteBinary`). The formats of Notes and Names take both data
  types, which is why the slip asks, rather than the format deciding.
- A file that cannot be written (a folder climbing out of the Files folder,
  a full disk) completes the item with the error -54101, "The file could
  not be written."; a "send now" item comes back to its routing slip, as
  protoTransport does for every transport. The Files folder never goes
  away, so nothing waits in the Out Box for a carrier.
- The file joins the Files soup at the next scan (`files.md`), so Find
  and the Files application see it.
- Demo `src/host/demo/savefiles.ns`, ctests `host.NewtonSaveFiles` and
  `.off`: two Notes saved as text (the second " 2"), a Names card saved as
  NSOF into Downloads, a refused folder, the slip with its two lines and
  the Folder pop-up, and the Notepad's own Action menu offering Files.

Not built (NOT YET): a note's ink and shapes as PDF or PNG, a picture as
its own file (the Gallery has no Action button yet), a `.nsof` opened from
Files put back into its application, the `send` permission for routing
without a tap; Internet mail, Mesh, Another Newton and Share to the host.

Found on the way: `DoPopup(items, left, top, view)` is a global function,
not a view method; a slip's child with a negative right edge needs
`viewJustify: 48` (`vjParentFullH`) or it has no width; the Action
menu's items are drawn, not views, and are found in the menu view's
inherited slots (`foreach ... deeply`); the menu shows a transport's
`title`, its slip's button the `actionTitle`.

### Internet mail (2026-11-12)

Built, behind the Host panel's "Internet mail" (`host/Features.h`'s
`kFeatureMail`, `NEWTON_MAIL`): a transport registered as the Files one
is (`src/host/HostMail.ns`, the global `HostMail:host`), so "Mail" is in
the Action button of every application with a text format.

- **Accounts** - the owner's choice: one main account, signed in by
  default, and secondary accounts beside it.
  - The main account is the one the host is set up with:
    `NEWTON_MAIL_ADDRESS`, `NEWTON_MAIL_NAME`, `NEWTON_MAIL_SMTP`,
    `NEWTON_MAIL_IMAP`, `NEWTON_MAIL_USER` (the address when unset) and
    `NEWTON_MAIL_PASSWORD`; its id is "main".
  - Secondary accounts are entries of the soup "Mail Accounts:host"
    (`{id, name, address, smtp, imap, user, main, addedBy}`). They are
    made through an API open to any application or package - the way a
    third party adds its own accounts:
    `GetGlobalVar('|HostMail:host|):AddAccount({name, address, smtp, imap,
    user, addedBy})` answers the id; `HostMailSetPassword(id, password)`
    gives the host the password; `RemoveAccount(id)` (which forgets the
    password too), `SetMain(id)` (`"main"` for the host's own again),
    `Accounts()`, `MainAccount()`, `AccountOf(id)`.
  - The main account is the one marked main, else the host's, else the
    first.
- **Passwords** are never in a soup. The host keeps them by account id
  (`host/HostMailKeys.cpp`): in Windows' Credential Manager ("Newton
  mail/<id>"), or in memory - gone when newton quits - with
  `NEWTON_MAIL_KEYSTORE=memory` (as the tests run, so they never touch the
  user's store) and on other hosts for now.
- **Sending:** the slip has the ROM's To line over the Names file's e-mail
  addresses (`protoAddressPicker`, `'|nameRef.email|`) and a From line, a
  pop-up of the accounts with the main one chosen. The item's text (its
  format's `TextScript`) is sent by SMTP to the To line's addresses and
  any in the item's `toAddresses` (for scripts), its title the subject.
  `host/HostMailFormat.cpp` makes the message: UTF-8, quoted-printable
  (so any server takes it), the subject and the sender's name as RFC 2047
  words when they are not ASCII. A failure - the server unreachable, the
  login refused - completes the item with -54102 and the server's reason
  ("The mail could not be sent: the mail server refused the login: 535
  ..."). A "send now" item comes back to its slip, and a queued one stays
  in the Out Box for its Send.
- **Receiving:** every `pollSeconds` (`NEWTON_MAIL_POLL`, 300), and when
  the In Box asks, each account with an IMAP server and a password is
  checked. New messages (`UID SEARCH UNSEEN`, at most 20 a check) are
  fetched and marked seen, and come into the In Box as Notes.
  - Each item's title is the subject, its `fromRef` the sender (a
    `'|nameRef.email|`), and its `mailAccount` the account the mail came
    to. `mailFrom`, `mailFromName`, `mailDate` and `mailText` are kept
    beside them.
  - The ROM's Put Away files them in the Notepad. They stay in the In Box
    until then: the transport's `defaultConfiguration` is
    `{dontAutoPutAway: 'never}` (the In Box's preferences can change it),
    because the Notepad would otherwise take each one at once.
  - Reading (`HostMailFormat.cpp`) unfolds headers and decodes RFC 2047
    words (B and Q). It takes the text/plain part, the first one inside
    multipart/alternative or mixed, or text/html with its tags taken out
    when there is no plain one. It undoes quoted-printable and base64,
    makes UTF-8 of ISO 8859-1 and Windows-1252, and names each attachment
    as "[attachment: name]".
- **Protocols** (`host/HostMailEngine.cpp`): SMTP (EHLO, AUTH PLAIN, MAIL,
  RCPT, DATA, QUIT) and IMAP (LOGIN, SELECT INBOX, UID SEARCH UNSEEN, UID
  FETCH BODY.PEEK[], UID STORE +FLAGS (\Seen), LOGOUT), as polled state
  machines over the IPP printers' socket (`print/host/HostIPPSocket.h`).
  `smtps://` and `imaps://` are TLS from the first byte, the certificate
  checked against the system's store (`NEWTON_MAIL_TRUST_ANY` for a test
  server); `smtp://` and `imap://` are plain.
- **Tests:**
  - ctest `host.MailFormat` (`src/host/tests/test_HostMail.cpp`): messages
    made and read back, and messages as other programs write them.
  - ctests `host.NewtonMail` and `.off` (`src/host/demo/mail.ns`) run
    against `tools/host/fakemail.py`, an SMTP and IMAP server whose
    waiting messages Python's own email package writes. They cover: a
    multipart message with an encoded subject received for the main
    account; a Note sent to itself and received back, non-ASCII text
    intact; a secondary account added through the API, receiving its own
    mail and sending from its own address; a wrong password refused with
    the server's reason; the slip's To and From; `SetMain`;
    `RemoveAccount`.

Not built (NOT YET): STARTTLS (ports 587 and 143), OAuth - which the large
providers (Gmail, Outlook) now require for IMAP and SMTP, so they cannot
be used yet; attachments either way; a settings slip for accounts (only
the environment and the API above for now); the Secret Service and the
Keychain; a reply from the In Box; the `send` and `network` permissions
(`permissions.md`); other folders than INBOX.

Found on the way: a received item for the Notepad is put away at once
unless the transport's config says `dontAutoPutAway: 'never` (the ROM's
In Box preferences set that slot); `protoTransport` keeps a slot
`lastError` of its own - a transport's error text must be named
otherwise; a script file given to `--script` is not read as UTF-8 -
write non-ASCII in its strings as `\u00E9\u`.
