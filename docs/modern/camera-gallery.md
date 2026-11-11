# A Camera and a Gallery

**Status: a proposal.**

## What Newton has, and has not

- No camera, and no application for pictures. Pictures appear inside notes,
  as bitmaps or PICTs.
- No file system. Data is entries in soups. Filing is one level: an entry's
  `labels` slot names one folder, and there are no folders inside folders.
  So there is no directory tree to put photographs in; one has to be
  designed.
- Large data: a *virtual binary object* keeps a big binary on the store and
  pages it in as needed (`store:NewVBO`; `docs/stores/README.md`). This is
  what holds a photograph.
- Screens of one, four or (with `--colour`) eight bits a pixel
  (`docs/qd/colour.md`).

## Pictures as data

A soup `Photos`. One entry a picture:

    {class: 'photo,
     image: <VBO, class 'jpeg: the file as taken, untouched>,
     thumb: <a small bitmap at the screen's depth, for lists>,
     width: 4000, height: 3000,
     timestamp: <minutes>, title: "…",
     album: "Trips/2026/Jordan",          // the folder tree, see below
     labels: nil,                          // ordinary filing still works
     source: 'camera | 'import | 'received,
     notes: <optional paragraph data, so a picture can be written on>}

The original file is kept byte for byte, so nothing is lost to the Newton's
screen depth and a picture exported is the picture taken. Other formats the
host can decode (PNG, HEIC where the host has it) are kept the same way
under their own class.

**The folder tree.** `album` is a path of names separated by `/`. There are
no folder objects: a folder exists while a picture's path begins with it,
plus a small soup `PhotoAlbums` for folders the user made and left empty.
An index on `album` makes "everything under Trips/" one query. Moving a
picture or renaming a folder rewrites paths. This gives the Gallery a real
tree without changing how filing works anywhere else.

**The tree outside the Newton.** The Files transport (`transports.md`)
exports and imports with the same paths as host directories, so a card or a
computer sees `Trips/2026/Jordan/IMG_0001.jpg`. Import of a camera card's
`DCIM` tree keeps its folders as albums.

## Decoding and drawing

Decoding JPEG is the host's work: a native `HostDecodeImage(vbo, width,
height)` answers a bitmap scaled to the size asked and reduced to the
screen's depth, dithered on one- and four-bit screens. The Gallery asks for
thumbnails once and stores them; a full view is decoded at the screen's
size and thrown away. The seam is `hal/host/HostImage.h`, with one portable
decoder vendored so every host has JPEG and PNG.

## The Camera

`hal/host/HostCamera.h`: list cameras, start a preview, give preview frames
as small bitmaps, take a picture and answer the file's bytes. Behind it:
Linux's V4L2, Windows' Media Foundation, the device's own on a tablet or
phone; and a **fake camera** for the desktop and the tests, which answers
frames and a file from a directory of test pictures.

The application: the preview filling the view, a shutter button, front and
back where there are two, a thumbnail of the last picture that opens the
Gallery. A picture taken becomes a `Photos` entry in the album "Camera".
Where the host has no camera the application is not installed.

On an e-paper screen the preview is slow by nature: the preview there is a
frame every second or so in grays, or none, with the picture shown after it
is taken.

## The Gallery

- The tree of albums on the left or in a picker on a small screen; a grid
  of thumbnails; a full view with next and previous by a flick.
- The Notepad's manners: the Action button routes a picture (print, beam,
  mail, Files), the filing button files it, Find searches titles and the
  writing on pictures.
- New folder, rename, move, delete, by the pop-up menu.
- **Write on a picture**: the pen on the full view adds ink over it, kept in
  `notes`, not burned into the image.
- **Into a note**: a picture dragged or routed to the Notepad is put in the
  note as a reference to the `Photos` entry with its thumbnail, not a copy.

Stationery: `'photo` is registered as a data type with its viewers
(`RegDataDef`), so the In Box can show a received picture and other
applications can show one in place.

## Camera use by other applications

A proto, "take a picture", any application can open: it answers a `Photos`
entry. The camera is a permission (`permissions.md`).

## Tests

With the fake camera: a picture taken appears in "Camera" with its
thumbnail; the file exported equals the file the fake gave, byte for byte;
folders made, renamed and moved, the queries right at each step; a `DCIM`
tree imported; a picture written on, restarted, the ink still there; the
grid at the four test screen sizes and at one, four and eight bits; a
24-megapixel file opened without the frames heap growing (it stays a VBO).

## Owner decisions

1. Video: not in this page. Wanted later?
2. Whether pictures sync (`sync.md`): they are large; default off.
3. One vendored decoder for all hosts, or each host's own.
