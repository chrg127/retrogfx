#include "retrogfx.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>

using u8  = uint8_t;
using u32 = uint32_t;

namespace retrogfx {

namespace {
    constexpr inline uint64_t bitmask(uint8_t nbits)
    {
        return (1UL << nbits) - 1UL;
    }

    constexpr inline uint64_t setbits(uint64_t num, uint8_t bitno, uint8_t nbits, uint64_t data)
    {
        const uint64_t mask = bitmask(nbits);
        return (num & ~(mask << bitno)) | (data & mask) << bitno;
    }

    constexpr inline uint64_t setbit(uint64_t num, uint8_t bitno, bool data)
    {
        return setbits(num, bitno, 1, data);
    }

    constexpr inline uint64_t getbits(uint64_t num, uint8_t bitno, uint8_t nbits)
    {
        return num >> bitno & bitmask(nbits);
    }

    constexpr inline uint64_t getbit(uint64_t num, uint8_t bitno)
    {
        return getbits(num, bitno, 1);
    }
}



namespace decoders {
    int planar(std::span<u8> tile, int y, int x, int bpp)
    {
        u8 nbit = 7 - x;
        u8 res = 0;
        for (int i = 0; i < bpp; i++)
            res = setbit(res, i, getbit(tile[y + i*8], nbit));
        return res;
    }

    int interwined(std::span<u8> tile, int y, int x, int bpp)
    {
        u8 nbit = 7 - x;
        u8 res = 0;
        for (int i = 0; i < bpp/2; i++) {
            res = setbit(res, i*2,     getbit(tile[i*16 + y*2    ], nbit));
            res = setbit(res, i*2 + 1, getbit(tile[i*16 + y*2 + 1], nbit));
        }
        if (bpp % 2 != 0) {
            auto i = bpp/2;
            res = setbit(res, i*2, getbit(tile[i*16 + y], nbit));
        }
        return res;
    }

    int gba(std::span<u8> tile, int y, int x, int bpp)
    {
        assert((bpp == 4 || bpp == 8)
            && "GBA format can't use BPP values that aren't 4 or 8");
        u8 version = getbit(bpp, 2); // 1 for 4, 0 for 8
        return getbits(tile[y * bpp + (x >> version)],
                       (x & version) << 2, bpp);
    }
} // namespace decoders

int decode_pixel(std::span<u8> tile, int row, int col, int bpp, Format mode)
{
    switch (mode) {
    case Format::Planar:     return decoders::planar(tile, row, col, bpp);
    case Format::Interwined: return decoders::interwined(tile, row, col, bpp);
    case Format::GBA:        return decoders::gba(tile, row, col, bpp);
    default:                 return 0;
    }
}

// when converting tiles, they are converted row-wise, i.e. first we convert
// the first row of every single tile, then the second, etc...
// decode_pixel()'s job is to do the conversion for one single tile
std::array<int, ROW_SIZE> decode_row(std::span<u8> tiles, int y, int num_tiles,
                                    int bpp, Format mode)
{
    int bpt = bpp*8;
    std::array<int, ROW_SIZE> res;
    // n = tile number; x = tile column
    for (int n = 0; n < TILES_PER_ROW; n++) {
        std::span<u8> tile = tiles.subspan(n*bpt, bpt);
        for (int x = 0; x < 8; x++)
            res[n*8 + x] = n < num_tiles ? decode_pixel(tile, y, x, bpp, mode) : 0;
    }
    return res;
}

void decode(std::span<uint8_t> bytes, int bpp, Format mode,
            std::function<void(std::span<int>)> draw_row)
{
    // this loop inspect at most 16 tiles each iteration
    // the inner loop gets one single row of pixels and draws it
    int bpt = bpp*8;
    for (std::size_t i = 0; i < bytes.size(); i += bpt * TILES_PER_ROW) {
        // calculate how many tiles we can get. can be at most TILES_PER_ROW
        // this is necessary in case we are at the end and the number of tiles
        // is not a multiple of TILES_PER_ROW.
        // division by bpt (bytes per tile) to go from bytes -> tiles
        std::size_t count     = std::min(bytes.size() - i,
                                         (std::size_t) bpt * TILES_PER_ROW);
        std::size_t num_tiles = count / bpt;
        std::span<u8> tiles   = bytes.subspan(i, count);
        for (int r = 0; r < TILE_HEIGHT; r++) {
            auto row = decode_row(tiles, r, num_tiles, bpp, mode);
            draw_row(row);
        }
    }
}



namespace encoders {
    std::array<u8, MAX_BPP> encode_planar_row(std::span<u8> row, int bpp)
    {
        std::array<u8, MAX_BPP> bytes;
        for (int i = 0; i < bpp; i++) {
            u8 byte = 0;
            for (int c = 0; c < 8; c++)
                byte = setbit(byte, 7-c, getbit(row[c], i));
            bytes[i] = byte;
        }
        return bytes;
    }

    void planar(std::span<u8> res, std::span<u8> row, int bpp, int y)
    {
        auto bytes = encode_planar_row(row, bpp);
        for (int x = 0; x < bpp; x++)
            res[y + x*8] = bytes[x];
    }

    void interwined(std::span<u8> res, std::span<u8> row, int bpp, int y)
    {
        auto bytes = encode_planar_row(row, bpp);
        for (int i = 0; i < bpp/2; i++) {
            res[i*16 + y*2    ] = bytes[i*2  ];
            res[i*16 + y*2 + 1] = bytes[i*2+1];
        }
        if (bpp % 2 != 0) {
            int i = bpp/2;
            res[i*16 + y] = bytes[i*2];
        }
    }

    void gba(std::span<u8> res, std::span<u8> row, int bpp, int y)
    {
        u8 version = getbit(bpp, 2); // 1 for 4, 0 for 8
        for (int x = 0; x < TILE_WIDTH; x++) {
            // res[y * 4 + (x >> version)] |= (row[x] << ((x & version) << 2));
            int i = y * 4 + (x >> version);
            res[i] = setbits(res[i], (x & version) << 2, 8 >> version, row[x]);
        }
    }
} // namespace encoders

std::array<u8, MAX_BPP*TILE_HEIGHT> encode_tile(Span2D<u8> tile, int bpp, Format format)
{
    std::array<u8, MAX_BPP*TILE_HEIGHT> res = {};
    for (auto y = 0u; y < TILE_HEIGHT; y++) {
        switch (format) {
        case Format::Planar:     encoders::planar(    res, tile[y], bpp, y); break;
        case Format::Interwined: encoders::interwined(res, tile[y], bpp, y); break;
        case Format::GBA:        encoders::gba(       res, tile[y], bpp, y); break;
        default: break;
        }
    }
    return res;
}

void encode(Span2D<u8> bytes, int bpp, Format format,
            std::function<void(std::span<uint8_t>)> write_data)
{
    if (bytes.width() % 8 != 0 || bytes.height() % 8 != 0) {
        std::fprintf(stderr, "error: width and height must be a power of 8");
        return;
    }
    for (auto y = 0u; y < bytes.height(); y += 8) {
        for (auto x = 0u; x < bytes.width(); x += 8) {
            auto tile = bytes.subspan(x, y, 8, 8);
            auto encoded = encode_tile(tile, bpp, format);
            std::span<u8> tilespan{encoded.begin(), encoded.begin() + bpp*8};
            write_data(tilespan);
        }
    }
}

long img_height(std::size_t num_bytes, int bpp)
{
    // We put 16 tiles on every row. If we have, for example, bpp = 2,
    // this corresponds to exactly 256 bytes for every row and means
    // we must make sure to have a multiple of 256.
    std::size_t bpt = bpp*8;
    std::size_t base = bpt * TILES_PER_ROW;
    num_bytes = num_bytes % base == 0 ? num_bytes : (num_bytes/base + 1) * base;
    return num_bytes / bpt / TILES_PER_ROW * 8;
}

void grayscale_palette(int bpp, std::function<void(u8)> f)
{
    const unsigned n = bpp_size(bpp);
    for (u8 t = 0; t < n; t++)
        f(0xFF / (n-1) * t);
}

} // namespace retrogfx
