# A Camera and a Gallery

**Status: a proposal.**

## What Newton has, and has not

- No camera, and no application for pictures. Pictures appear inside notes,
  as bitmaps or PICTs.
- No file system. Data is entries in soups. Filing is one level: an entry's
  `labels` slot names one folder, and there are no folders inside folders.
  So there is no directory tree to put photographs in; `files.md`
  supplies a real one.
- Large data: a *virtual binary object* keeps a big binary on the store and
  pages it in as needed (`store:NewVBO`; `docs/stores/README.md`). A file's
  content is read the same way (`files.md`).
- Screens of one, four or (with `--colour`) eight bits a pixel
  (`docs/qd/colour.md`).

## Pictures as data

A picture is a file (`files.md`): an entry of the `Files` soup under
`Pictures/`, its `content` the file as taken, untouched. The Gallery is a
view of the files whose type is a picture, wherever they are; a drive with
a camera's `DCIM` tree shows its pictures while it is in.

What a picture adds to a file's entry, kept in the file store's side
directory:

    thumb: <a small bitmap at the screen's depth, for lists>,
    width: 4000, height: 3000, taken: <minutes, from the file's own data>,
    source: 'camera | 'import | 'received,
    notes: <optional paragraph data, so a picture can be written on>

The original file is kept byte for byte, so nothing is lost to the Newton's
screen depth, and any other system reading the disk sees
`Pictures/Trips/2026/Jordan/IMG_0001.jpg`.

**Albums are folders**: the file entry's `path`. Making, renaming and
moving albums are the Files application's operations on directories. This
is the folder tree the Newton's one-level filing does not have, and it is
the real one.

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
Gallery. A picture taken becomes a file in `Pictures/Camera`.
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
  note as a reference to the file's entry with its thumbnail, not a copy.

Stationery: `'photo` is registered as a data type with its viewers
(`RegDataDef`), so the In Box can show a received picture and other
applications can show one in place.

## Camera use by other applications

A proto, "take a picture", any application can open: it answers the new
file's entry. The camera is a permission (`permissions.md`).

## Tests

With the fake camera: a picture taken appears in "Camera" with its
thumbnail; the file exported equals the file the fake gave, byte for byte;
folders made, renamed and moved, the directory agreeing at each step; a
`DCIM` tree on a second file store shown; a picture written on, restarted, the ink still there; the
grid at the four test screen sizes and at one, four and eight bits; a
24-megapixel file opened without the frames heap growing (it stays a file).

## Owner decisions

1. Video: not in this page. Wanted later?
2. Pictures are files, so the Newton's sync does not carry them
   (`files.md`); a file-sync service on the host would. Enough?
3. One vendored decoder for all hosts, or each host's own.
