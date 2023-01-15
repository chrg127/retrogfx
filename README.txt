RETROGFX

retrogfx.hpp is library for conversion of graphics files from older consoles
(NES, SNES, GBA ...).
The library supports any bpp (bits per pixel) value between 1-8 and two formats:

    - Planar (used on the NES);
    - Interwined (used on the SNES);
    - GBA (used, well, on the GBA);

It also offers some palette support.

INSTALLING

The library is composed of two files (a .cpp and a .hpp file), which can be
found in the lib/ directory. To install, simply drop these in your project,
making sure to add them on your build system too.

DOCUMENTATION

The documentation is written in the header file. There are comments for usage
for each function. An example program is also provided inside the directory
example/.

FUTURE PLANS

Although the library is mostly finished, I plan in the future to research other
consoles' formats and support them.
