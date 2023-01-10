#include "retrogfx.hpp"

#include <algorithm>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <memory>

using u8  = uint8_t;
using u32 = uint32_t;

namespace retrogfx {

const int TILES_PER_ROW = 16;
const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 8;
const int ROW_SIZE = TILES_PER_ROW * TILE_WIDTH;
const int MAX_BPP = 8;

template <typename T, std::size_t Width, std::size_t Height>
class Array2D {
    static_assert(Width != 0 || Height != 0, "Can't define an Array2D with 0 width or height");
    T arr[Width*Height];
public:
    constexpr std::span<T>       operator[](std::size_t pos)       { return std::span{ arr+pos*Width, Width }; }
    constexpr std::span<const T> operator[](std::size_t pos) const { return std::span{ arr+pos*Width, Width }; }
    constexpr T *data() { return arr; }
    constexpr const T *data() const { return arr; }
    constexpr std::size_t width()  const { return Width; }
    constexpr std::size_t height() const { return Height; }
};

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

    constexpr unsigned pow2(unsigned n)
    {
        int r = 1;
        while (n-- > 0)
            r *= 2;
        return r;
    }

    template <unsigned BPP>
    constexpr std::array<RGB, pow2(BPP)> make_default_palette()
    {
        constexpr unsigned N = pow2(BPP);
        constexpr uint8_t base = 0xFF / (N-1);
        std::array<RGB, N> palette;
        for (unsigned i = 0; i < N; i++) {
            uint8_t value = base * i;
            palette[i] = RGB(value, value, value);
        }
        return palette;
    }

    const auto palette_1bpp = make_default_palette<1>();
    const auto palette_2bpp = make_default_palette<2>();
    const auto palette_3bpp = make_default_palette<3>();
    const auto palette_4bpp = make_default_palette<4>();
    const auto palette_8bpp = make_default_palette<8>();

    RGB make_color(std::span<u8> data)
    {
        switch (data.size()) {
        case 1: return RGB(data[0], data[0], data[0]);
        case 3: return RGB(data[0], data[1], data[2]);
        default:
            fprintf(stderr, "warning: unsupported number of channels (%ld)\n", data.size());
            return RGB();
        }
    }

    int find_color(std::span<const RGB> p, RGB c)
    {
        auto it = std::find(p.begin(), p.end(), c);
        return it != p.end() ? it - p.begin() : -1;
    }
}



/* decoding functions (gfx data -> image) */

namespace {

    // the 'planar' format is a format that takes each pixel's value
    // and splits them into more 'bit-planes':
    // first bit-plane is a collection of the least significant bits
    // last bit-plane is a collection of the most significant bits
    // nth bit-plane is a collection of the nth bits (for each pixel)
    u8 decode_pixel_planar(std::span<u8> tile, int y, int x, int bpp)
    {
        u8 nbit = 7 - x;
        u8 res = 0;
        for (int i = 0; i < bpp; i++) {
            u8 byte = tile[y + i*8];
            u8 bit  = getbit(byte, nbit);
            res     = setbit(res, i, bit);
        }
        return res;
    }

    u8 decode_pixel_interwined(std::span<u8> tile, int y, int x, int bpp)
    {
        u8 nbit = 7 - x;
        u8 res = 0;
        for (int i = 0; i < bpp/2; i++) {
            u8 lowbyte = tile[i*16 + y*2    ];
            u8 hibyte  = tile[i*16 + y*2 + 1];
            u8 lowbit  = getbit(lowbyte, nbit);
            u8 hibit   = getbit(hibyte,  nbit);
            res        = setbit(res, i*2,     lowbit);
            res        = setbit(res, i*2 + 1, hibit);
        }
        if (bpp % 2 != 0) {
            int i = bpp/2;
            u8 byte = tile[i*16 + y];
            u8 bit  = getbit(byte, nbit);
            res     = setbit(res, i*2, bit);
        }
        return res;
    }

    // gba format (4 bpp): 32 bytes per tile, each byte contains the value for
    // two pixels.
    u8 decode_pixel_gba(std::span<u8> tile, int y, int x, int bpp)
    {
        assert(bpp == 4);
        int value = tile[y * 4 + (x/2)];
        return getbits(value, x & 1 ? 4 : 0, 4);
    }

    u8 decode_pixel(std::span<u8> tile, int row, int col, int bpp, DataMode mode)
    {
        switch (mode) {
        case DataMode::Planar:     return decode_pixel_planar(tile, row, col, bpp);
        case DataMode::Interwined: return decode_pixel_interwined(tile, row, col, bpp);
        case DataMode::GBA:        return decode_pixel_gba(tile, row, col, bpp);
        default:                   return 0;
        }
    }

    // when converting tiles, they are converted row-wise, i.e. first we convert
    // the first row of every single tile, then the second, etc...
    // decode_pixel()'s job is to do the conversion for one single tile
    std::array<u8, ROW_SIZE> decode_row(std::span<u8> tiles, int y, int num_tiles,
                                        int bpp, DataMode mode)
    {
        int bpt = bpp*8;
        std::array<u8, ROW_SIZE> res;
        // n = tile number; x = tile column
        for (int n = 0; n < TILES_PER_ROW; n++) {
            std::span<u8> tile = tiles.subspan(n*bpt, bpt);
            for (int x = 0; x < 8; x++)
                res[n*8 + x] = n < num_tiles ? decode_pixel(tile, y, x, bpp, mode) : 0;
        }
        return res;
    }
} // namespace



/* encoding functions (image -> gfx data) */

namespace {
    // encode single row of tile, returns a byte for each plane
    std::array<u8, MAX_BPP> encode_row(std::span<u8> row, int bpp)
    {
        std::array<u8, MAX_BPP> bytes;
        for (int i = 0; i < bpp; i++) {
            u8 byte = 0;
            for (int c = 0; c < 8; c++) {
                u8 bits = row[c];
                byte = setbit(byte, 7-c, getbit(bits, i));
            }
            bytes[i] = byte;
        }
        return bytes;
    }

    void encode_tile_planar(std::span<u8> res, std::span<u8> bytes, int bpp, int y)
    {
        for (int x = 0; x < bpp; x++)
            res[y + x*8] = bytes[x];
    }

    void encode_tile_interwined(std::span<u8> res, std::span<u8> bytes, int bpp, int y)
    {
        for (int i = 0; i < bpp/2; i++) {
            res[i*16 + y*2    ] = bytes[i*2  ];
            res[i*16 + y*2 + 1] = bytes[i*2+1];
        }
        if (bpp % 2 != 0) {
            int i = bpp/2;
            res[i*16 + y] = bytes[i*2];
        }
    }

    // loop over the rows of a single tile, returns bytes of encoded tile. si = start index
    std::array<u8, MAX_BPP*8> encode_tile(std::span<u8> tiles, std::size_t si, std::size_t width, int bpp, DataMode mode)
    {
        std::array<u8, MAX_BPP*8> res;
        for (int y = 0; y < TILE_HEIGHT; y++) {
            std::size_t ri = si + y*width;
            auto bytes = encode_row(tiles.subspan(ri, TILE_WIDTH), bpp);
            switch (mode) {
            case DataMode::Planar:     encode_tile_planar(    res, bytes, bpp, y); break;
            case DataMode::Interwined: encode_tile_interwined(res, bytes, bpp, y); break;
            default: break;
            }
        }
        return res;
    }
}



void decode(std::span<uint8_t> bytes, int bpp, DataMode mode, Callback draw_row)
{
    // this loop inspect at most 16 tiles each iteration
    // the inner loop gets one single row of pixels and draws it
    int bpt = bpp*8;
    for (std::size_t i = 0; i < bytes.size(); i += bpt * TILES_PER_ROW) {
        // calculate how many tiles we can get. can be at most 16 (TILES_PER_ROW)
        // this is necessary in case we are at the end and the number of tiles
        // is not a multiple of 16.
        // division by bpt (bytes per tile) to go from bytes -> tiles
        std::size_t count     = std::min(bytes.size() - i,
                                         (std::size_t) bpt * TILES_PER_ROW);
        std::size_t num_tiles = count / bpt;
        std::span<u8> tiles = bytes.subspan(i, count);
        for (int r = 0; r < TILE_HEIGHT; r++) {
            auto row = decode_row(tiles, r, num_tiles, bpp, mode);
            draw_row(row);
        }
    }
}

void encode(std::span<u8> bytes, std::size_t width, std::size_t height, int bpp,
            DataMode mode, Callback write_data)
{
    if (width % 8 != 0 || height % 8 != 0) {
        std::fprintf(stderr, "error: width and height must be a power of 8");
        return;
    }

    for (std::size_t j = 0; j < bytes.size(); j += width*TILE_WIDTH) {
        for (std::size_t i = 0; i < width; i += TILE_WIDTH) {
            auto tile = encode_tile(bytes, j+i, width, bpp, mode);
            std::span<u8> tilespan{tile.begin(), tile.begin() + bpp*8};
            write_data(tilespan);
        }
    }
}

void make_indexed(std::span<uint8_t> data, std::span<const RGB> palette, int channels, std::function<void(int)> output)
{
    for (std::size_t i = 0; i < data.size(); i += channels) {
        auto index = find_color(palette, make_color(data.subspan(i, channels)));
        if (index == -1) {
            fprintf(stderr, "warning: color not present in palette\n");
            output(0);
        } else
            output(index);
    }
}

void apply_palette(std::span<int> data, std::span<const RGB> palette, std::function<void(RGB)> output)
{
    for (std::size_t i = 0; i < data.size(); i++)
        output(palette[i]);
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

std::span<const RGB> grayscale_palette(int bpp)
{
    switch (bpp) {
    case 1:  return palette_1bpp;
    case 2:  return palette_2bpp;
    case 3:  return palette_3bpp;
    case 4:  return palette_4bpp;
    case 5:  return make_default_palette<5>();
    case 6:  return make_default_palette<6>();
    case 7:  return make_default_palette<7>();
    case 8:  return palette_8bpp;
    default:
        fprintf(stderr, "no default palette bpp of value %d\n", bpp);
        return std::span<const RGB>{};
    }
}

} // namespace retrogfx
