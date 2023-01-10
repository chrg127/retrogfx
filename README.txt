retrogfx.hpp is library for conversion of graphics files from older consoles
(NES, SNES, GBA ...). The library supports any bpp (bits per pixel) value
between 1-8 and two "data modes" (refers to how bytes are laid out in a tile):

    - Planar (more straightforward, used for example by the NES);
    - Interwined (used by the SNES).

It also offers some palette support.
Although the library is mostly finished, I plan in the future to research other
consoles' formats and support them.
