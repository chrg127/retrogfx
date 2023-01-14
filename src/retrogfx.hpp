#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
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
     * This format takes each pixel's value
     * and splits them into more 'bit-planes':
     * first bit-plane is a collection of the least significant bits;
     * last bit-plane is a collection of the most significant bits;
     * in general nth bit-plane is a collection of the nth bits,
     * all for each pixel
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

using RGB = std::array<uint8_t, 3>;

template <typename T>
concept ContainerType = requires(T t) {
    t.data();
    t.size();
};

template <typename T>
struct Span2D {
    T *d;
    std::size_t w, h, s;

    constexpr Span2D() : d(nullptr), w(0), h(0), s(0) {}
    constexpr Span2D(T *data, std::size_t width, std::size_t height, std::size_t stride)
        : d(data), w(width), h(height), s(stride)
    { }

    constexpr Span2D(T *d, std::size_t w, std::size_t h) : Span2D(d, w, h, 0) { }

    // constructor for taking a 1D collection.
    template <typename R>
    constexpr Span2D(R &&r, std::size_t w, std::size_t h)
        requires requires (R a) { a.data(); }
        : Span2D(r.data(), w, h)
    { }

    // constructor for taking a container<container<T>>.
    template <typename R>
    constexpr Span2D(R &&r)
        requires requires(R a) { a.data(); a.size(); a[0].data(); a[0].size(); }
        : Span2D((T *) r[0].data(), r[0].size(), r.size(), 0)
    { }

    constexpr Span2D(const Span2D &) noexcept = default;
    constexpr Span2D& operator=(const Span2D &) noexcept = default;

    using element_type      = T;
    using value_type        = std::remove_cv_t<T>;
    using size_type         = std::size_t;
    using reference         = std::span<T>;

    constexpr reference front()                 const          { return this->operator[](0); }
    constexpr reference back()                  const          { return this->operator[](w-1); }
    constexpr reference operator[](size_type y) const          { return std::span{&d[y * (w + s)], w}; }
    constexpr T *       data()                  const          { return d; }
    constexpr size_type width()                 const noexcept { return w; }
    constexpr size_type height()                const noexcept { return h; }
    constexpr size_type stride()                const noexcept { return s; }
    [[nodiscard]] constexpr bool empty()        const noexcept { return w == 0 || h == 0; }

    constexpr Span2D<T> subspan(size_type x, size_type y, size_type width, size_type height) const
    {
        return Span2D(&d[y * (w + s) + x], width, height, s + (w - width));
    }
};

/*
 * Decodes a given array of bytes into an indexed image.
 * format describes the format of the bytes.
 * bpp describes how many bits per pixel to use (most formats accept any
 * number of bits per pixel, some may not).
 * draw_row is a callback function that will be called for each row of the
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
 * bytes is a 2D view over the array of bytes.
 * bpp is the bits per pixel to use; some formats may not support the given bpp.
 * write_data is a function that is called for each tile encoded. It has as
 * input the data of one tile encoded.
 */
void encode(
    Span2D<uint8_t> bytes,
    int bpp,
    Format format,
    std::function<void(std::span<uint8_t>)> write_data
);

/* Helper function for encode(). */
inline void encode(
    std::span<uint8_t> bytes, std::size_t width, std::size_t height, int bpp,
    Format format, std::function<void(std::span<uint8_t>)> write_data
)
{
    encode(Span2D<uint8_t>{bytes, width, height}, bpp, format, write_data);
}

/*
 * Creates an indexed image suitable for usage with encode(). To do so, it uses
 * a palette: each pixel of the image is looked up in the palette and, if found,
 * is sent a callback function.
 * data is the data of the image, assumed to be a collection of colors (but not
 * an image)
 * palette is the palette to use.
 * output is a function that is called for each pixel, with input an index
 * into the palette.
 * data.width() and palette.width() is the number of channels the function uses.
 * Both values must be equal. It is up to the caller to make sure they match.
 * The function's return value is:
 *     -2 on channel mismatch
 *     -1 on success
 *     >= 0 if a color was not found, with the value indicating the index
 *     into the data.
 */
int make_indexed(
    Span2D<uint8_t> data,
    Span2D<uint8_t> palette,
    std::function<void(std::size_t)> output
);

/* Same function as above, but it takes the channels as a template constant. */
template <unsigned Channels>
int make_indexed(
    std::span<std::array<uint8_t, Channels>> data,
    std::span<const std::array<uint8_t, Channels>> palette,
    std::function<void(std::size_t)> output
)
{
    for (auto c = 0u; c < data.size(); c += 1) {
        std::size_t index = -1;
        for (int i = 0; i < palette.size(); i++)
            if (palette[i] == data[c])
                index = i;
        if (index == -1)
            return c;
        output(index);
    }
    return -1;
}

/*
 * Applies a palette to an indexed image. The reverse of the function above.
 * data is the data of the image, assumed to be an indexed image.
 * palette is the palette to use.
 * output is a callback function that is called for each index, with input
 * a color.
 */
void apply_palette(
    std::span<std::size_t> data,
    std::span<const RGB> palette,
    std::function<void(RGB)> output
);

/*
 * A helper function to calculate the height of the resulting image when
 * decoding. Before allocating space for an image, this function should be
 * used to find the resulting height (width should always be ROW_SIZE).
 * @num_bytes is the size of the data to decode.
 * @bpp is, as usual, the bytes per pixel.
 */
long img_height(std::size_t num_bytes, int bpp);

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

/*
 * A helper function that returns the size for a palette of @bpp color depth,
 * useful when constructing palettes with the function above.
 */
constexpr inline int bpp_size(int bpp) { return std::pow(bpp, 2); }

} // namespace retrogfx
