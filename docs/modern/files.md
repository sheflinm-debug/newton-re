# Files: real files as Newton entries, and viewing other systems' formats

**Status: a proposal; first pieces built (2026-11-12) - the Files
application over a host folder, text and Markdown made notes; every file
an entry of the `Files` soup, found by Find, renamed, moved and put in
the trash from the soup (State).** It assumes a host with a file system of its own
(`docs/modernization-plan.md`: a Linux kernel under the Newton, or any
desktop host). Facts about third-party libraries here are from general
knowledge, not checked for this page; licences in particular must be read
before anything is vendored.

## What Newton has

No file system: data is entries in soups on stores. A store that comes and
goes (a card) is merged into every union soup while it is in. Large data is
a virtual binary object, paged in from the store (`docs/stores/README.md`).
Data types have viewers registered as stationery (`RegDataDef`). The dock
has translators that turn desktop formats into Newton data.

## The aim

A document downloaded, received or on a USB stick is simply there on the
Newton, found by Find, sent by the Action button, opened by a tap; and the
same bytes are an ordinary file to any other system that reads the disk.
No import step, no copies.

## Files as entries

**The file store.** A host directory is presented as a store of a new
kind, `TFileStore`, beside the flash and card stores. It holds one soup,
`Files`. Each file under the directory is an entry:

    {class: 'file,
     name: "Report.pdf", path: "Documents/Work",      // the folder tree
     type: '|application/pdf|,                         // from the name and the content
     size: 482113, created: <minutes>, modified: <minutes>,
     content: <a binary backed by the file itself>,
     labels: nil,
     meta: {title: …, pages: …}}                       // filled by the type's reader, kept beside the file

- `content` is not a copy: reading it reads the file, through the same
  paging a VBO uses. A large file never enters the frames heap.
- The entry is made from the directory, not stored: the store watches the
  directory (the host's change notification) and adds, changes and removes
  entries, sending the ordinary soup-change notifications.
- What a file cannot hold (`labels`, marks, ink written over it, `meta`) is
  kept in a hidden side directory of the store, keyed by the file, so other
  systems see only clean files.
- Changing `name` or `path` renames or moves the file; removing the entry
  moves the file to a trash folder, emptied on request.

**The tree.** `path` is the directory relative to the store's root; an
index on it makes "everything under Documents/" one query. Standard roots
on the internal file store: `Documents`, `Downloads`, `Pictures`, `Music`,
`Received`.

**Drives are stores.** A USB stick, an SD card, a network share or a
file-sync folder mounted by the host appears as another file store, shown
as a card is: its files join the `Files` union soup while it is present and
leave when it goes. The Newton's "this card belongs to another Newton"
manners do not apply; a file store is never formatted or adopted.

**The Files application.** In Extras: the tree, a list or grid with
thumbnails, the filing and Action buttons, Find, new folder, rename, move,
copy between stores, the trash. A tap opens the file with its viewer.

**Arrivals.** A download, a mail attachment, a file sent by another Newton
or put in the In Box and "Put away" lands in `Downloads` or `Received` as
a file.

## Newton data as files, the other way

Second step. A directory `Newton/` in the internal file store shows soup
entries as files other systems can read, made on demand: a note as text or
PDF, a Names card as vCard, a meeting as iCalendar. Dropping a `.vcf` or
`.ics` there adds the card or meeting. These are translators, one a
format, registered as the dock's are.

## Foreign formats

Three levels, in order of how much each gives for its cost.

**1. Translate into Newton data** where a Newton equivalent exists. The
result is editable, searchable and small, and needs no library at view
time.

| Format | Becomes |
|---|---|
| plain text, Markdown, RTF, simple HTML | a note's paragraphs (styles kept where the Newton has them) |
| CSV | a table (`structure.md`) |
| vCard, iCalendar | Names cards, meetings and to-dos |
| EPUB | a Newton book, or paragraphs (`docs/books/`) |
| Word `.docx`, OpenDocument text | a note: the text, headings, lists and tables extracted from the file's XML; layout is not kept |

**2. View faithfully, as pages.** One host service,
`hal/host/HostDocument.h`: open a file, answer its page count and each
page's size, draw page *n* at a given scale into a bitmap at the screen's
depth, answer a page's text with positions (for Find and selection).
A viewer stationery in NewtonScript shows the pages, scrolls and zooms,
and lets the pen write over them (ink kept in the side directory, exported
as PDF annotations on request).

| Format | Behind the service |
|---|---|
| PDF | a PDF renderer: PDFium (permissive licence), or MuPDF or Poppler (copyleft licences that would bind the whole distribution) |
| images: JPEG, PNG, GIF, WebP; HEIC where the host has it | the image decoder of `camera-gallery.md`; a picture is a one-page document |
| Word, Excel, PowerPoint, OpenDocument | converted to PDF by LibreOffice run headless, where it is installed: an optional component of several hundred megabytes, absent on small devices |
| sound and video | the host's codecs (FFmpeg or the system's), played through the Newton's sound path; video in a view where the screen can keep up |
| ZIP and other archives | shown as a folder |

**3. Everything else** is still a file: listed with its type and size,
routed, filed, synced, sent to another system. The Newton does not need to
understand a file to carry it.

**Safety.** Readers of foreign formats are where hostile files do their
damage. The document service runs in its own process with no network and
only the one file open; a crash there costs one viewer, not the Newton.

## What changes elsewhere

- `camera-gallery.md`: pictures are files under `Pictures/`; the Gallery is
  a view of them.
- `transports.md`: the Files transport becomes "Save a copy to a folder";
  attachments are files.
- `sync.md`: file stores are not synced by the Newton's sync; a file-sync
  service on the host does that job for files.
- `permissions.md`: `files` becomes a grant, by root (`Documents`,
  `Pictures`, all).

## Tests

A temporary directory as a file store: files appear as entries; one added
from outside appears, with its notification; renamed and moved from the
Newton, the directory agrees; a 100 MB file listed and partly read with
the heap unchanged; a second directory attached and detached like a card;
Find by name and by a PDF's text; a `.vcf` translated into a card and a
card read back as the same `.vcf`. The document service against a fixed
set of files, pages compared with reference bitmaps; a damaged file (the
service dies, the Newton reports and carries on).

## State (2026-11-12, branch `modern`)

Built, behind the Host panel's "Files" (`host/Features.h`'s
`kFeatureFiles`, `NEWTON_FILES`): the Files application in Extras
(`src/host/HostFiles.ns`, the global `HostFiles:host`) over a folder of the
host's - `NEWTON_FILES_ROOT`, or "Newton Files" in the user's home, made
with Documents, Downloads, Pictures, Music and Received. It lists a
folder's folders and files (kind from the name's ending, size, date), opens
folders, and makes a note of a text, Markdown or CSV file (the first
level, "translate into Newton data": paragraphs split at blank lines, a
Markdown heading in the Notepad's font made bold; the note keeps the
file's path in `fromFile`; the file is untouched). Natives
(`host/HostFiles.cpp` over `host/HostFilesOS.cpp`, which keeps
`<windows.h>` away from the Newton headers): `HostFilesRoot`,
`HostFilesList`, `HostFilesReadText`, `HostFilesWriteText`,
`HostFilesMakeFolder`; a path is relative to the folder and one that
climbs out (`..`) or names a drive is refused. Demo `src/host/demo/files.ns`,
ctests `host.NewtonFiles` and `.off`.

Files as entries (`HostFiles.ns`'s Sync, SoupChanged, OpenEntry): every
file under the folder is an entry of the soup `Files` on the internal
store - `{class: 'file, name, path, kind, size, modified, labels, disk,
excerpt}`, an index on `path`. Hidden files and folders (a leading `.`)
are not entries. Every three seconds `HostFilesScan(signature)` walks the
tree (at most 5000 files, 16 folders deep) and answers nil when its
signature - every name, size and modification time - is unchanged;
otherwise the soup is brought into step (entries added, changed,
removed, without a notification each) and one `'whatThe` is sent. Find
finds files (`RegFindApps`; the application's `Find` is the ROM's own
`StandardFind` over the soup): by name, and for a text file by the words
of its first 2000 characters (`excerpt`). A found text file opens as a
note (the application's `OpenTo`; without it the ROM's finder opens the
Files application first, which would hide the Notepad). Changing an
entry's `name` or `path` from another application (`EntryChangeXmit`
with its own symbol) renames or moves the file (`HostFilesMove`, never
over a file that is there; refused, the entry goes back);
`EntryRemoveFromSoupXmit` moves the file to `.Trash`, a number added
when the name is taken. `disk`, where the file was last seen, makes each
change happen once in whatever order the deferred notifications and the
scans come; a scan never changes an entry's name or path. The demo's
second half and ctest `host.NewtonFiles` check each of these, Find
through the Find slip itself.

How these depart from the design above (NOT YET): the entries are a soup
of the internal store kept in step, not a store of their own
(`TFileStore`), so they are a copy of the listing and a file's content is
read through the natives, not paged as a binary; the folder is looked at
on a timer, not watched with the host's change notification; labels are
kept in the entry, not beside the file; folders are not entries; when the
feature is turned off the soup is left (the next start brings it into
step); two files whose names differ only in case are one entry (the
reconciliation keys entries by symbol); a change made with no application
symbol is not seen (the ROM sends no notification for it).

Not built: `TFileStore` and drives as stores; emptying the trash, and
renaming, moving and new folders from the Files application itself;
arrivals landing in Downloads/Received; Newton data as files; the
document service (level 2) and every reader beyond plain text.

Found on the way: the Notepad's notes are `class: 'paperroll`,
`viewStationery: 'paperroll`, their paragraphs' fonts packed integers
(18435, bold with 1 << 20) - a note made with `'paper` and a font frame
opens empty. `to` is a NewtonScript keyword: a local named `to` is a
syntax error. The ROM's finder calls an owner's `OpenTo(entry, finder)`
when it has one, instead of opening it and calling `ShowFoundItem`.

## Owner decisions

1. Copyleft libraries: allowed (more choice, binds how the whole thing may
   be distributed) or permissive only.
2. Whether the LibreOffice route is wanted at all, given its size.
3. Whether file names and folders may be changed from the Newton on
   removable drives, or those are read-only by default.
