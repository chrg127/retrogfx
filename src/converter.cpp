#include <cstdio>
#include <cstdint>
#include <cassert>
#include <array>
#include <span>
#include <string>
#include <optional>
#include <charconv>
#include <string_view>
#include <memory>
#include <algorithm>
#include <fmt/core.h>
#undef None
#include "stb_image.h"
#include "stb_image_write.h"
#include "retrogfx.hpp"
#include "cmdline.hpp"

template <typename TStr = std::string>
std::optional<int> to_number(const TStr &str, unsigned base = 10)
{
    int value = 0;
    auto res = std::from_chars(str.data(), str.data() + str.size(), value, base);
    if (res.ec != std::errc() || res.ptr != str.data() + str.size())
        return std::nullopt;
    return value;
}

int encode_image(std::string_view input, std::string_view output, int bpp, retrogfx::Format format)
{
    int width, height, channels;
    unsigned char *img_data = stbi_load(input.data(), &width, &height, &channels, 0);
    if (!img_data) {
        fmt::print(stderr, "error: couldn't load image {}\n", input);
        return 1;
    }

    FILE *out = fopen(output.data(), "w");
    if (!out) {
        fmt::print(stderr, "error: couldn't write to {}\n", output);
        std::perror("");
        return 1;
    }

    auto pal = retrogfx::grayscale_palette(bpp);
    auto tmp = std::span(img_data, width*height*channels);
    std::vector<uint8_t> data;
    retrogfx::make_indexed(tmp, pal, channels, [&](int x) { data.push_back(x); });
    retrogfx::encode(data, width, height, bpp, format, [&](std::span<uint8_t> tile) {
        fwrite(tile.data(), 1, tile.size(), out);
    });

    fmt::print("done\n");
    fclose(out);
    return 0;
}

long filesize(FILE *f)
{
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long res = ftell(f);
    fseek(f, pos, SEEK_SET);
    return res;
}

int decode_to_image(std::string_view input, std::string_view output, int bpp, retrogfx::Format format)
{
    FILE *f = fopen(input.data(), "r");
    if (!f) {
        fmt::print(stderr, "error: couldn't open file {}: ", input);
        std::perror("");
        return 1;
    }
    long size = filesize(f);
    auto ptr = std::make_unique<uint8_t[]>(size);
    std::fread(ptr.get(), 1, size, f);
    auto bytes = std::span{ptr.get(), std::size_t(size)};

    size_t height = retrogfx::img_height(size, bpp);
    auto img_data = std::make_unique<uint8_t[]>(retrogfx::ROW_SIZE * height * 3);
    std::fill(img_data.get(), img_data.get() + retrogfx::ROW_SIZE * height * 3, 0);

    auto pal = retrogfx::grayscale_palette(bpp);
    int y = 0;
    retrogfx::decode(bytes, bpp, format, [&](std::span<uint8_t> row) {
        for (int x = 0; x < retrogfx::ROW_SIZE; x++) {
            const auto color = pal[row[x]];
            auto i = y * retrogfx::ROW_SIZE * 3 + x * 3;
            for (auto c = 0u; c < 3; c++)
                img_data[i + c] = color[c];
        }
        y++;
    });

    stbi_write_png(output.data(), retrogfx::ROW_SIZE, height, 3, img_data.get(), 0);
    fclose(f);

    return 0;
}

std::optional<int> parse_bpp(cmdline::Result &result)
{
    if (!result.has('b'))
        return std::nullopt;
    auto &p = result.params['b'];
    auto num = to_number(p);
    if (!num)
        fmt::print(stderr, "warning: invalid value {} for -b (default of 2 will be used)\n", p);
    if (num.value() == 0 || num.value() > 8) {
        fmt::print(stderr, "warning: bpp can only be 1 to 8 (default of 2 will be used)\n");
        return std::nullopt;
    }
    return num;
}

std::optional<retrogfx::Format> parse_format(cmdline::Result &result)
{
    if (result.has('f')) {
        auto &p = result.params['f'];
        if (auto r = retrogfx::string_to_format(p); r)
            return r;
        else
            fmt::print(stderr, "warning: invalid argument {} for -f (default \"planar\" will be used)\n", p);
    }
    return std::nullopt;
}

using cmdline::ParamType;

static const cmdline::Argument arglist[] = {
    { 'h', "help",      "show this help text"                                      },
    { 'o', "output",    "FILENAME: output to FILENAME",          ParamType::Single },
    { 'r', "reverse",   "convert from image to chr"                                },
    { 'b', "bpp",       "NUMBER: specify bpp (bits per pixel)",  ParamType::Single },
    { 'f', "format", "(planar | interwined): specify format",    ParamType::Single },
};

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fmt::print(stderr, "usage: chrconvert [file...]\n");
        cmdline::print_args(arglist);
        return 1;
    }

    auto result = cmdline::parse(argc, argv, arglist);
    if (result.has('h')) {
        fmt::print(stderr, "usage: chrconvert [file...]\n");
        cmdline::print_args(arglist);
        return 0;
    }

    if (result.items.size() == 0) {
        fmt::print(stderr, "error: no file specified\n");
        fmt::print(stderr, "usage: chrconvert [file...]\n");
        cmdline::print_args(arglist);
        return 1;
    } else if (result.items.size() > 1) {
        fmt::print(stderr, "error: too many files specified (only first will be used)\n");
    }

    auto input  = result.items[0];
    enum class Mode { ToImg, ToBin };
    auto mode   = result.has('r') ? Mode::ToBin : Mode::ToImg;
    auto output = result.has('o')     ? result.params['o']
                : mode == Mode::ToImg ? "output.png"
                :                       "output.bin";
    int bpp = parse_bpp(result).value_or(2);
    retrogfx::Format format = parse_format(result).value_or(retrogfx::Format::Planar);

    return mode == Mode::ToImg ? decode_to_image(input, output, bpp, format)
                               : encode_image(   input, output, bpp, format);
}
