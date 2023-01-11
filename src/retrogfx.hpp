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

inline std::optional<Format> string_to_format(std::string_view s)
{
    if (s == "planar")      return Format::Planar;
    if (s == "interwined")  return Format::Interwined;
    if (s == "gba")         return Format::GBA;
    return std::nullopt;
}

using RGB = std::array<uint8_t, 3>;

void decode(std::span<uint8_t> bytes, int bpp, Format mode, Callback draw_row);
void encode(std::span<uint8_t> bytes, std::size_t width, std::size_t height, int bpp, Format mode, Callback write_data);
void make_indexed(std::span<uint8_t> data, std::span<const RGB> palette, int channels, std::function<void(int)> output);
void apply_palette(std::span<std::size_t> data, std::span<const RGB> palette, std::function<void(RGB)> output);
long img_height(std::size_t num_bytes, int bpp);
std::span<const RGB> grayscale_palette(int bpp);

} // namespace retrogfx
