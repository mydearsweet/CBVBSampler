#pragma once
// ---------------------------------------------------------------------------------------------
// NCW (Native Instruments Compressed Wave) decoder.
//
// NCW is a lossless DPCM + bit-truncation container.  This is an independent implementation
// written from the publicly documented format (MIT/Apache-2.0 "ncw" reverse-engineering effort).
// It only reads plain, unprotected .ncw audio files; it does not touch encrypted Kontakt content.
//
// Layout (all little-endian unless noted):
//   120-byte file header:  8-byte magic (big-endian 0x01A89ED63?010000, ? = 0 or 1),
//                          u16 channels, u16 bitsPerSample, u32 sampleRate, u32 numFrames,
//                          u32 blocksOffset, u32 dataOffset, u32 dataSize, ...
//   block table @ blocksOffset: ceil(numFrames/512) u32 offsets (relative to dataOffset)
//                          plus one terminal entry equal to dataSize
//   per 512-frame group:   one block per channel.  Each block = 16-byte header
//                          { u32 magic (BE) 0x160C9A3E, i32 base, i16 bits, u16 flags, 4 pad }
//                          followed by |bits| * 64 bytes of LSB-first packed signed values.
//                            bits > 0 : deltas, sample[0] = base, sample[i+1] = sample[i] + delta[i]
//                            bits < 0 : raw samples at |bits| bits each
//                            bits = 0 : legacy raw samples at the file bit depth
//                          flags bit0 = mid/side stereo, bit1 = 32-bit float data
// ---------------------------------------------------------------------------------------------
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace ncw
{
constexpr size_t headerSize = 120;
constexpr size_t blockHeaderSize = 16;
constexpr size_t framesPerBlock = 512;

struct Decoded
{
    int channels = 0;
    int bitsPerSample = 0;
    double sampleRate = 0.0;
    int64_t numFrames = 0;
    bool isFloat = false;
    // One vector per channel.  PCM: sample values left-aligned into 32 bits (i.e. << (32 - bits)).
    // Float: raw IEEE-754 bit patterns.  Either way the layout matches what a JUCE
    // AudioFormatReader::readSamples() is expected to hand back.
    std::vector<std::vector<int32_t>> data;
};

namespace detail
{
inline uint32_t rdLE32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rdLE16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
inline uint32_t rdBE32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }

inline int32_t signExtend (uint32_t raw, int bits)
{
    const int shift = 32 - bits;
    return (int32_t) (raw << shift) >> shift;
}

// Unpack `count` LSB-first signed values of `bits` width (1..32) from data[0..len).
inline bool unpack (const uint8_t* data, size_t len, int bits, size_t count, std::vector<int32_t>& out)
{
    out.clear();
    out.reserve (count);
    uint64_t acc = 0;
    int avail = 0;
    size_t pos = 0;
    const uint64_t mask = (bits == 32) ? 0xFFFFFFFFull : ((1ull << bits) - 1ull);

    while (out.size() < count)
    {
        while (avail < bits)
        {
            if (pos >= len)
                return false;
            acc |= (uint64_t) data[pos++] << avail;
            avail += 8;
        }
        out.push_back (signExtend ((uint32_t) (acc & mask), bits));
        acc >>= bits;
        avail -= bits;
    }
    return true;
}
} // namespace detail

// Decode a complete .ncw file held in memory.  Returns false and fills `error` on any problem.
inline bool decode (const uint8_t* file, size_t fileSize, Decoded& out, std::string& error)
{
    using namespace detail;
    auto fail = [&error] (const char* m) { error = m; return false; };

    if (file == nullptr || fileSize < headerSize)
        return fail ("File is too small to be an NCW file");

    // Magic is stored big-endian in the first 8 bytes.
    const uint32_t magicHi = rdBE32 (file);
    const uint32_t magicLo = rdBE32 (file + 4);
    if (magicHi != 0x01A89ED6u || (magicLo != 0x31010000u && magicLo != 0x30010000u))
        return fail ("Not an NCW file (bad signature)");

    const int channels = rdLE16 (file + 8);
    const int bits = rdLE16 (file + 10);
    const uint32_t sampleRate = rdLE32 (file + 12);
    const uint32_t numFrames = rdLE32 (file + 16);
    const uint32_t blocksOffset = rdLE32 (file + 20);
    const uint32_t dataOffset = rdLE32 (file + 24);
    const uint32_t dataSize = rdLE32 (file + 28);

    if (channels < 1 || channels > 8)                       return fail ("Unsupported NCW channel count");
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return fail ("Unsupported NCW bit depth");
    if (sampleRate < 1000 || sampleRate > 768000)           return fail ("Implausible NCW sample rate");
    if (blocksOffset < headerSize || dataOffset < blocksOffset) return fail ("Corrupt NCW header offsets");

    const uint64_t numBlocks = ((uint64_t) numFrames + framesPerBlock - 1) / framesPerBlock;
    if ((uint64_t) dataOffset - blocksOffset != (numBlocks + 1) * 4)
        return fail ("NCW block table does not match the frame count");
    if ((uint64_t) dataOffset + dataSize > fileSize)
        return fail ("NCW file is truncated");
    if ((uint64_t) numFrames * (uint64_t) channels * 4ull > (1ull << 30))
        return fail ("NCW file is too large to load");

    std::vector<uint32_t> offsets ((size_t) numBlocks + 1);
    for (size_t i = 0; i < offsets.size(); ++i)
        offsets[i] = rdLE32 (file + blocksOffset + i * 4);
    if (offsets.back() != dataSize)
        return fail ("NCW terminal block offset does not match the data size");

    out = Decoded();
    out.channels = channels;
    out.bitsPerSample = bits;
    out.sampleRate = (double) sampleRate;
    out.numFrames = (int64_t) numFrames;
    out.data.assign ((size_t) channels, std::vector<int32_t>());
    for (auto& c : out.data)
        c.reserve ((size_t) numFrames);

    std::vector<int32_t> values;
    std::vector<std::vector<int32_t>> group ((size_t) channels);
    bool formatKnown = false;

    for (size_t b = 0; b < (size_t) numBlocks; ++b)
    {
        const uint64_t start = (uint64_t) dataOffset + offsets[b];
        const uint64_t end = (uint64_t) dataOffset + offsets[b + 1];
        if (end < start || end > fileSize)
            return fail ("Corrupt NCW block offsets");

        uint64_t pos = start;
        bool midSide = false;

        for (int ch = 0; ch < channels; ++ch)
        {
            if (pos + blockHeaderSize > end)
                return fail ("NCW block runs past its group");
            const uint8_t* h = file + pos;
            if (rdBE32 (h) != 0x160C9A3Eu)
                return fail ("Bad NCW block signature");

            const int32_t base = (int32_t) rdLE32 (h + 4);
            const int16_t blockBits = (int16_t) rdLE16 (h + 8);
            const uint16_t flags = rdLE16 (h + 10);
            pos += blockHeaderSize;

            const bool blockFloat = (flags & 2u) != 0;
            if (! formatKnown)
            {
                out.isFloat = blockFloat;
                formatKnown = true;
            }
            else if (blockFloat != out.isFloat)
                return fail ("NCW sample format changes between blocks");

            if (ch == 0)
            {
                midSide = (flags & 1u) != 0;
                if (midSide && channels != 2)
                    return fail ("NCW mid/side requires exactly two channels");
            }

            const int absBits = blockBits < 0 ? -(int) blockBits : (int) blockBits;
            if (absBits > 32)
                return fail ("Unsupported NCW block width");

            auto& dest = group[(size_t) ch];
            dest.clear();

            if (blockBits == 0)
            {
                // Legacy raw layout at the file's bit depth.
                const size_t bytesPer = (size_t) bits / 8;
                const size_t need = bytesPer * framesPerBlock;
                if (pos + need > end)
                    return fail ("NCW block runs past its group");
                dest.reserve (framesPerBlock);
                for (size_t i = 0; i < framesPerBlock; ++i)
                {
                    uint32_t raw = 0;
                    for (size_t k = 0; k < bytesPer; ++k)
                        raw |= (uint32_t) file[pos + i * bytesPer + k] << (8 * k);
                    dest.push_back (signExtend (raw, (int) bytesPer * 8));
                }
                pos += need;
            }
            else
            {
                const size_t need = (size_t) absBits * framesPerBlock / 8;
                if (pos + need > end)
                    return fail ("NCW block runs past its group");
                if (! unpack (file + pos, need, absBits, framesPerBlock, values))
                    return fail ("Corrupt NCW block payload");
                pos += need;

                if (blockBits > 0)
                {
                    int32_t current = base;
                    dest.reserve (framesPerBlock);
                    for (size_t i = 0; i < framesPerBlock; ++i)
                    {
                        dest.push_back (current);
                        current = (int32_t) ((uint32_t) current + (uint32_t) values[i]); // wrapping add
                    }
                }
                else
                    dest = values;
            }
        }

        if (pos != end)
            return fail ("Unconsumed bytes inside an NCW block group");

        if (midSide)
        {
            for (size_t i = 0; i < framesPerBlock; ++i)
            {
                const int32_t m = group[0][i], s = group[1][i];
                if (out.isFloat)
                {
                    float fm, fs;
                    static_assert (sizeof (float) == sizeof (int32_t), "float size");
                    std::memcpy (&fm, &m, 4);
                    std::memcpy (&fs, &s, 4);
                    const float l = fm + fs, r = fm - fs;
                    std::memcpy (&group[0][i], &l, 4);
                    std::memcpy (&group[1][i], &r, 4);
                }
                else
                {
                    group[0][i] = (int32_t) ((uint32_t) m + (uint32_t) s);
                    group[1][i] = (int32_t) ((uint32_t) m - (uint32_t) s);
                }
            }
        }

        const size_t frames = (size_t) std::min<uint64_t> (framesPerBlock, numFrames - (uint64_t) b * framesPerBlock);
        for (int ch = 0; ch < channels; ++ch)
            out.data[(size_t) ch].insert (out.data[(size_t) ch].end(), group[(size_t) ch].begin(),
                                          group[(size_t) ch].begin() + (std::ptrdiff_t) frames);
    }

    // Left-align integer PCM into 32 bits (what AudioFormatReader::readSamples expects).
    if (! out.isFloat)
    {
        const int shift = 32 - bits;
        for (auto& c : out.data)
            for (auto& v : c)
                v = (int32_t) ((uint32_t) v << shift);
    }
    else if (bits != 32)
        return fail ("NCW float data must be 32-bit");

    return true;
}
} // namespace ncw
