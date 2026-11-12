# A Camera and a Gallery

**Status: a proposal; a first version built (2026-11-12) - the Gallery over
the Files soup's pictures, the Camera over a fake camera, and a picture
decoder of our own (State).**

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

## State (2026-11-12, branch `modern`)

Built, behind the Host panel's "Camera and Gallery" (`host/Features.h`'s
`kFeatureGallery`, `NEWTON_GALLERY`):

- **Decoding** (`src/hal/host/HostImage.h`, `HostImage.cpp`): a decoder
  written for this project from the formats' specifications - no library,
  nothing vendored, which leaves owner decision 3 open (one vendored
  decoder, or each host's own) and owner decision 1 of `files.md`
  (copyleft) untouched. PNG: every colour type and depth, palettes,
  transparency (over white), Adam7. JPEG: baseline and extended Huffman,
  gray or YCbCr, any sampling (chroma filled in between its samples as
  libjpeg does), restart intervals. A picture is scaled by averaging into
  the ROM's grays (`RGBtoGray`'s weights, white 0) and packed at 1 bit
  (error diffusion), 2, 4 or 8. Checked by ctest `hal.HostImage` against
  Pillow's decoding of the same files (`tools/host/picturefixtures.py`):
  every PNG identical, every JPEG within 3 levels (mean 0.3). Refused,
  by name: progressive and arithmetic JPEG, 12-bit, CMYK, JPEGs in several
  scans; GIF, WebP and HEIC are not read.
- **The camera** (`src/hal/host/HostCamera.h`): the fake camera only -
  `NEWTON_CAMERA=<folder>` makes one of a folder of pictures (three in
  `src/host/demo/camera`); its preview is the picture in front of it, a
  picture taken is that file's bytes, and then the next is in front. With
  no camera the Camera application is not installed.
- **Natives** (`src/host/HostPictures.cpp`): `HostPictureInfo(path)`,
  `HostPictureBitmap(path, width, height, depth)` (a bitmap shape fitted
  inside the box, never enlarged), `HostCameraName()`,
  `HostCameraPreview(width, height, depth)`, `HostCameraTake(folder)`
  (writes `IMG_<date>_<time>.jpg` into that folder of the Files folder and
  answers its name). Paths are the Files folder's (`HostFilesPaths.h`).
- **The Gallery** (`src/host/HostGallery.ns`, Extras): a view of the
  `Files` soup's entries whose kind is JPEG or PNG, wherever they are;
  albums are folders (the entries' `path`). It shows the albums (a
  thumbnail, the folder, how many), an album's grid of thumbnails, and a
  picture filling the view, a tap on its left or right third going to the
  one before or after and the title line going back. A thumbnail (56
  pixels, four bits) is decoded once and kept in the entry (`thumb`, with
  `thumbOf` the modification time it was made from, so a changed file
  gets a new one); a full view is decoded at the view's size and thrown
  away. A picture tapped in the Files application or found by Find opens
  in the Gallery (`HostFiles.ns`'s `ShowInGallery`). The Files soup is
  kept while Files or the Gallery is on, so the Gallery works with Files
  off (the Files application's icon then stays away).
- **The Camera** (`src/host/HostGallery.ns`, Extras, only with a camera):
  the preview, refreshed every second; the shutter writes the file into
  `Pictures/Camera` and brings it into the Files soup at once; the last
  picture's thumbnail opens it in the Gallery.
- Demo `src/host/demo/gallery.ns`, ctests `host.NewtonGallery` and `.off`:
  two pictures taken, each the fake camera's file to the byte (by size),
  the last opened from the Camera, one moved into `Pictures/Trips` making a
  second album, then the albums, the grid and a picture shown.

Not built (NOT YET): writing on a picture, routing and filing (the Action
and filing buttons), the `'photo` data type (`RegDataDef`), a picture put
into a note by reference, new folder, rename, move and delete from the
Gallery's own pop-up, a flick for next and previous, a camera's `DCIM`
tree on a drive (drives are not stores yet - `files.md`), the "take a
picture" proto for other applications, the camera as a permission, a
real camera (V4L2, Media Foundation, a device's own), EXIF (the time
taken, the orientation), colour on the eight-bit screen (pictures are
drawn in grays at every depth), a test at the four screen sizes and three
depths, and a 24-megapixel file (a whole file is decoded in the host's
memory, not the frames heap; at most 128 MB is opened).

Found on the way: a `clView` gets no `viewClickScript` without
`vClickable` (512) in its flags; a bitmap shape's `bounds` is a
`'boundsRect` binary, not a frame - `ShapeBounds(shape)` gives the frame.
