#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace retrogfx {

const int TILES_PER_ROW = 16;
const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 8;
const int ROW_SIZE = TILES_PER_ROW * TILE_WIDTH;
const int MAX_BPP = 8;

enum class Format {
    /*
     * This format takes each pixel's value and splits them into more bitplanes:
     * first bit-plane is a collection of the least significant bits;
     * last bit-plane is a collection of the most significant bits;
     * in general the nth bit-plane is a collection of the nth bits, all for
     * each pixel.
     */
    Planar,

    /* A format utilized on the SNES. */
    Interwined,

    /* The format used on the GBA. There are two versions:
     * - 4 BPP: 32 bytes per tile (4 for each row). Each byte encodes
     *   two pixels: the 4 higher bits encode the pixel on the right, while the
     *   lower ones encode the pixel on the left.
     * - 8 BPP: 64 bytes per tile, (8 for each row).
     *   Each byte encodes one pixel.
     */
    GBA,
};

inline std::optional<Format> string_to_format(std::string_view s)
{
    if (s == "planar")      return Format::Planar;
    if (s == "interwined")  return Format::Interwined;
    if (s == "gba")         return Format::GBA;
    return std::nullopt;
}

inline std::optional<std::string_view> format_to_string(Format f)
{
    switch (f) {
    case Format::Planar:     return "planar";
    case Format::Interwined: return "interwined";
    case Format::GBA:        return "gba";
    default:                 return std::nullopt;
    }
}

/*
 * Decodes a given array of bytes into an indexed image.
 * @bytes are the bytes to decode.
 * @format describes the format of the bytes.
 * @bpp describes how many bits per pixel to use (most formats accept any
 * number of bits per pixel, some may not).
 * @draw_row is a callback function that will be called for each row of the
 * resulting image. It has as input an array of indexes.
 */
void decode(
    std::span<uint8_t> bytes,
    int bpp,
    Format format,
    std::function<void(std::span<int>)> draw_row
);

/*
 * Encodes a given array of bytes, formatted as an indexed image, to a given
 * format.
 * @indices is the indexed image. @width and @height are self-explanatory.
 * @bpp is the bits per pixel to use; some formats may not support the given bpp.
 * @format describes the format in which the bytes should be encoded.
 * @write_data is a function that is called for each tile encoded. It has as
 * input the data of one tile encoded.
 */
void encode(
    std::span<uint8_t> indices,
    std::size_t width,
    std::size_t height,
    int bpp,
    Format format,
    std::function<void(std::span<uint8_t>)> write_data
);

/* Finds @color in @palette. Returns the index or -1 if not found. */
template <typename T>
int find_color(std::span<T> palette, std::span<uint8_t> color)
{
    for (auto i = 0u; i < palette.size(); i++)
        if (!std::memcmp(palette[i].data(), color.data(), color.size()))
            return i;
    return -1;
}

/*
 * Creates an indexed image suitable for usage with encode(). To do so, it uses
 * a palette: each pixel of the image is looked up in the palette and, if found,
 * is sent a callback function.
 * @data is the data of the image, assumed to be a collection of colors of size
 * being a multiple of @channels.
 * @palette is the palette to use.
 * @channels indicates how many channels or components the colors use.
 * @output is a function that is called for each pixel, with input an index
 * into the palette.
 * The function's return value is -1 on success, >= 0 if a color was not found,
 * with the value indicating the index.
 */
template <typename T>
int make_indexed(
    std::span<uint8_t> data,
    std::span<T> palette,
    int channels,
    std::function<void(std::size_t)> output
)
{
    assert(palette[0].size() == channels && "mismatched channels");
    assert(data.size() % channels == 0 && "size of data not a multiple of channels");
    for (auto c = 0u; c < data.size(); c += channels) {
        auto i = find_color(palette, data.subspan(c, channels));
        if (i == -1)
            return c;
        output(i);
    }
    return -1;
}

/*
 * Applies a palette to an indexed image. The reverse of the functions above.
 * data is the data of the image, assumed to be an indexed image.
 * @data is a collection of indexes.
 * @palette is the palette to use.
 * @output is a callback function that is called for each index, with input
 * a color.
 */
template <typename T>
void apply_palette(
    std::span<std::size_t> data,
    std::span<T> palette,
    int channels,
    std::function<void(T)> output
)
{
    for (auto i : data)
        output(palette[i]);
}

/*
 * A helper function to calculate the height of the resulting image when
 * decoding. Before allocating space for an image, this function should be
 * used to find the resulting height (width should always be ROW_SIZE).
 * @num_bytes is the size of the data to decode.
 * @bpp is the bytes per pixel the data uses.
 */
long img_height(std::size_t num_bytes, int bpp);

/* A helper function that returns the size for a palette of @bpp color depth. */
constexpr inline int bpp_size(int bpp) { return std::pow(bpp, 2); }

/*
 * Helper functions that returns a grayscale palette for use when decoding.
 * This one takes @BPP and number of @Channels as constants.
 */
template <unsigned BPP, unsigned Channels, unsigned N = bpp_size(BPP)>
constexpr std::array<std::array<uint8_t, Channels>, N> grayscale_palette()
{
    std::array<std::array<uint8_t, Channels>, N> palette;
    for (auto t = 0u; t < N; t++) {
        auto value = uint8_t(0xFF / (N-1) * t);
        if constexpr(Channels == 1) palette[t] = std::array<uint8_t, 1>{value};
        if constexpr(Channels == 2) palette[t] = std::array<uint8_t, 2>{value, 0xFF};
        if constexpr(Channels == 3) palette[t] = std::array<uint8_t, 3>{value, value, value};
        if constexpr(Channels == 4) palette[t] = std::array<uint8_t, 4>{value, value, value, 0xFF};
    }
    return palette;
}

/*
 * This one is for when the bpp value is not known beforehand.
 * It returns palette values through the @output callback.
 */
void grayscale_palette(int bpp, std::function<void(uint8_t)> output);

} // namespace retrogfx
