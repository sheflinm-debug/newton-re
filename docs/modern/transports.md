# Transports: new ways to send, in every application at once

**Status: a proposal; the first transport built (2026-11-12) - Files,
"Save a copy to a folder" (State).**

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
   or to a mail application like Messages.

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
