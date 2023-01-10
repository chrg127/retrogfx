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
#include <fmt/core.h>
#include <CImg.h>
#undef None
#include "stb_image.h"
#include "retrogfx.hpp"
#include "cmdline.hpp"

template <typename T = int, typename TStr = std::string>
inline std::optional<T> to_number(const TStr &str, unsigned base = 10)
{
    auto helper = [](const char *start, const char *end, unsigned base) -> std::optional<T> {
        T value = 0;
        std::from_chars_result res;
        if constexpr(std::is_floating_point_v<T>)
            res = std::from_chars(start, end, value);
        else
            res = std::from_chars(start, end, value, base);
        if (res.ec != std::errc() || res.ptr != end)
            return std::nullopt;
        return value;
    };
    if constexpr(std::is_same<std::decay_t<TStr>, char *>::value)
        return helper(str, str + std::strlen(str), base);
    else
        return helper(str.data(), str.data() + str.size(), base);
}

int image_to_chr(const char *input, const char *output, int bpp, retrogfx::DataMode mode)
{
    int width, height, channels;
    unsigned char *img_data = stbi_load(input, &width, &height, &channels, 0);
    if (!img_data) {
        fmt::print(stderr, "error: couldn't load image {}\n", input);
        return 1;
    }

    FILE *out = fopen(output, "w");
    if (!out) {
        fmt::print(stderr, "error: couldn't write to {}\n", output);
        std::perror("");
        return 1;
    }

    auto pal = retrogfx::grayscale_palette(bpp);
    auto tmp = std::span(img_data, width*height*channels);
    std::vector<uint8_t> data;
    retrogfx::make_indexed(tmp, pal, channels, [&](int x) { data.push_back(x); });
    retrogfx::encode(data, width, height, bpp, mode, [&](std::span<uint8_t> tile) {
        fwrite(tile.data(), 1, tile.size(), out);
    });

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

int chr_to_image(const char *input, const char *output, int bpp, retrogfx::DataMode mode)
{
    FILE *f = fopen(input, "r");
    if (!f) {
        fmt::print(stderr, "error: couldn't open file {}: ", input);
        std::perror("");
        return 1;
    }
    long size = filesize(f);
    auto ptr = std::make_unique<uint8_t[]>(size);
    std::fread(ptr.get(), 1, size, f);
    auto bytes = std::span{ptr.get(), std::size_t{size}};

    size_t height = retrogfx::img_height(size, bpp);
    cimg_library::CImg<unsigned char> img(16 * 8, height, 1, 4);

    img.fill(0);
    auto pal = retrogfx::grayscale_palette(bpp);
    int y = 0;
    retrogfx::decode(bytes, bpp, mode, [&](std::span<uint8_t> row) {
        for (int x = 0; x < 128; x++) {
            const auto color = pal[row[x]];
            img(x, y, 0) = color.r;
            img(x, y, 1) = color.g;
            img(x, y, 2) = color.b;
            img(x, y, 3) = 0xFF;
        }
        y++;
    });

    img.save_png(output);
    fclose(f);

    return 0;
}

std::optional<std::pair<int, retrogfx::DataMode>> select_mode(std::string_view arg)
{
    if (arg == "nes")
        return std::make_pair(2, retrogfx::DataMode::Planar);
    if (arg == "snes")
        return std::make_pair(4, retrogfx::DataMode::Interwined);
    return std::nullopt;
}

using cmdline::ParamType;

static const std::vector<cmdline::Argument> arglist = {
    { 'h', "help",      "show this help text"                                         },
    { 'o', "output",    "FILENAME: output to FILENAME",             ParamType::Single },
    { 'r', "reverse",   "convert from image to chr"                                   },
    { 'b', "bpp",       "NUMBER: specify bpp (bits per pixel)",     ParamType::Single },
    { 'd', "data-mode", "(planar | interwined): specify data mode", ParamType::Single },
    { 'm', "mode",      "(nes | snes): specify mode",               ParamType::Single },
};

int main(int argc, char *argv[])
{
    auto usage = []() {
        fmt::print(stderr, "usage: chrconvert [file...]\n");
        cmdline::print_args(arglist);
    };

    if (argc < 2) {
        usage();
        return 1;
    }

    enum class Mode { TOIMG, TOCHR } mode = Mode::TOIMG;
    const char *input = NULL, *output = NULL;
    int bpp = 2;
    retrogfx::DataMode datamode = retrogfx::DataMode::Planar;

    auto result = cmdline::parse(argc, argv, arglist);
    if (result.has('h')) {
        usage();
        return 0;
    }
    if (result.has('o'))
        output = result.params['o'].data();
    if (result.has('r'))
        mode = Mode::TOCHR;
    if (result.has('b')) {
        auto num = to_number(result.params['b']);
        if (!num)
            fmt::print(stderr, "warning: invalid value {} for -b (default of 2 will be used)\n", argv[0]);
        else if (num.value() == 0 || num.value() > 8)
            fmt::print(stderr, "warning: bpp can only be 1 to 8 (default of 2 will be used)\n");
        else
            bpp = num.value();
    }
    if (result.has('d')) {
        if (auto r = retrogfx::string_to_datamode(result.params['d']); r)
            datamode = r.value();
        else
            fmt::print(stderr, "warning: invalid argument {} for -d (default \"planar\" will be used)\n", argv[0]);
    }
    if (result.has('m')) {
        if (auto o = select_mode(result.params['m']); o) {
            bpp = o.value().first;
            datamode = o.value().second;
        } else
            fmt::print(stderr, "warning: invalid mode (defaults will be used)\n");
    }

    if (result.items.size() == 0) {
        fmt::print(stderr, "error: no file specified\n");
        usage();
        return 1;
    } else if (result.items.size() > 1)
        fmt::print(stderr, "error: too many files specified (only first will be used)\n");
    input = result.items[0].data();

    return mode == Mode::TOIMG ? chr_to_image(input, output ? output : "output.png", bpp, datamode)
                               : image_to_chr(input, output ? output : "output.bin", bpp, datamode);
}
