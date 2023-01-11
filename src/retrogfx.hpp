#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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

using Callback = std::function<void(std::span<uint8_t>)>;

enum class Format {
    Planar,
    Interwined,
    GBA,
};

template <std::size_t N> struct ColorData;
#define COLOR_DATA(N, ...)                                                      \
    template <> struct ColorData<N> {                                           \
        union { struct { uint8_t __VA_ARGS__; }; std::array<uint8_t, N> d; };   \
        constexpr ColorData(auto... args) : d{args...} {}                       \
    };
COLOR_DATA(3, r, g, b)
COLOR_DATA(4, r, g, b, a)
#undef COLOR_DATA

template <std::size_t N>
struct Color : ColorData<N> {
    Color() = default;
    Color(auto... args) : ColorData<N>(args...) {}
    Color(uint32_t value)
    {
        for (auto i = 0u; i < N; i++)
            this->d[i] = value >> ((N - 1 - i) * 8) & 0xff;
    }
    uint8_t & operator[](std::size_t i) { return this->d[i]; }
    template <std::size_t M> friend bool operator==(Color<M> a, Color<M> b);
};

template <std::size_t N> bool operator==(Color<N> a, Color<N> b) { return a.d == b.d; }

using RGBA = Color<4>;
using RGB  = Color<3>;

inline std::optional<Format> string_to_format(std::string_view s)
{
    if (s == "planar")      return Format::Planar;
    if (s == "interwined")  return Format::Interwined;
    if (s == "gba")         return Format::GBA;
    return std::nullopt;
}

void decode(std::span<uint8_t> bytes, int bpp, Format mode, Callback draw_row);
void encode(std::span<uint8_t> bytes, std::size_t width, std::size_t height, int bpp, Format mode, Callback write_data);
void make_indexed(std::span<uint8_t> data, std::span<const RGB> palette, int channels, std::function<void(int)> output);
void apply_palette(std::span<std::size_t> data, std::span<const RGB> palette, std::function<void(RGB)> output);
long img_height(std::size_t num_bytes, int bpp);
std::span<const RGB> grayscale_palette(int bpp);

} // namespace retrogfx
