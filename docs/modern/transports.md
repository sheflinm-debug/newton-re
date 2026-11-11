# Transports: new ways to send, in every application at once

**Status: a proposal.**

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
| **Files** | anything, written to a host directory: text as `.txt`, a note's ink and shapes as PDF or PNG, a frame as NSOF, a picture as its original file | a host native for the directory; the PostScript and page drawing the print path already has |
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
