# Files: real files as Newton entries, and viewing other systems' formats

**Status: a proposal.** It assumes a host with a file system of its own
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

## Owner decisions

1. Copyleft libraries: allowed (more choice, binds how the whole thing may
   be distributed) or permissive only.
2. Whether the LibreOffice route is wanted at all, given its size.
3. Whether file names and folders may be changed from the Newton on
   removable drives, or those are read-only by default.
