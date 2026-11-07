// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// embed_bank.cpp - writes a hybrid bank image as the C++ array the synthkeys engine links against
//
// synthkeys compiles its banks into the engine as generated sources (src/core/hybrid_*_bank.cpp, made by its
// tools/samples/embed_bank.py). As text they are 30-110 MB, too large for the repository, so ToneWerk keeps the bank images
// (engine/synthkeys/banks/*.bank) and generates the same sources at build time:
//
//   embed_bank <image.bank> <header include> <symbol> <out.cpp>
//
// The array is 16-byte aligned (HybridBank::attach requires it); its size is the constant the header declares.
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf (stderr, "usage: embed_bank <image.bank> <header include> <symbol> <out.cpp>\n");
        return 2;
    }
    std::FILE* in = std::fopen (argv[1], "rb");
    if (in == nullptr)
    {
        std::fprintf (stderr, "embed_bank: cannot read %s\n", argv[1]);
        return 1;
    }
    std::vector<unsigned char> data;
    unsigned char buf[1 << 16];
    for (size_t n; (n = std::fread (buf, 1, sizeof (buf), in)) > 0;)
        data.insert (data.end(), buf, buf + n);
    std::fclose (in);

    std::FILE* out = std::fopen (argv[4], "wb");
    if (out == nullptr)
    {
        std::fprintf (stderr, "embed_bank: cannot write %s\n", argv[4]);
        return 1;
    }
    std::fprintf (out,
        "// GENERATED at build time by engine/tools/embed_bank.cpp from %s, DO NOT EDIT\n"
        "#include \"%s\"\n\nnamespace pmp {\n\nalignas(16) const uint8_t %s[%sSize] = {\n",
        argv[1], argv[2], argv[3], argv[3]);
    static const char* hex = "0123456789abcdef";
    std::vector<char> line;
    for (size_t i = 0; i < data.size(); i += 32)
    {
        line.clear();
        for (size_t k = i; k < data.size() && k < i + 32; ++k)
        {
            const char item[] = { '0', 'x', hex[data[k] >> 4], hex[data[k] & 15], ',' };
            line.insert (line.end(), item, item + sizeof (item));
        }
        line.push_back ('\n');
        std::fwrite (line.data(), 1, line.size(), out);
    }
    std::fprintf (out, "};\n\n} // namespace pmp\n");
    const bool ok = std::fclose (out) == 0;
    return ok ? 0 : 1;
}
