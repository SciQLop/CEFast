#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <zlib.h>

#include "cefast/errors.hpp"

namespace cefast
{
namespace detail
{
    struct deflate_guard
    {
        z_stream* stream;
        ~deflate_guard() { deflateEnd(stream); }
    };

    struct inflate_guard
    {
        z_stream* stream;
        ~inflate_guard() { inflateEnd(stream); }
    };

    // zlib takes 32-bit counters; larger buffers are fed in chunks.
    constexpr std::size_t zlib_chunk = std::numeric_limits<uInt>::max();

    inline bool all_zero(const void* data, std::size_t size)
    {
        if (size == 0)
            return true;
        const auto* bytes = static_cast<const unsigned char*>(data);
        return std::all_of(bytes, bytes + size, [](unsigned char byte) { return byte == 0; });
    }
}

// gzip (RFC 1952) member: used by the tests and by a future writer.
[[nodiscard]] inline std::string gzip(std::string_view plain)
{
    if (plain.size() > detail::zlib_chunk)
        throw std::length_error("cefast: gzip input larger than 4 GiB");
    z_stream stream { };
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)
        != Z_OK)
        throw std::runtime_error("cefast: zlib deflateInit2 failed");
    const detail::deflate_guard guard { &stream };

    std::string output(deflateBound(&stream, static_cast<uLong>(plain.size())), '\0');
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(plain.data()));
    stream.avail_in = static_cast<uInt>(plain.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
        throw std::runtime_error("cefast: zlib deflate failed");
    output.resize(stream.total_out);
    return output;
}

// Whole-buffer gunzip: the output size is not in the CEF file, so the buffer grows by doubling
// (4x the compressed size as a first guess). Concatenated members are all decompressed, and the
// zero padding some writers append after the last member is accepted.
[[nodiscard]] inline std::string gunzip(std::span<const char> compressed, std::string_view source)
{
    if (compressed.size() < 2)
        throw parse_error(source, 0, "corrupt or truncated gzip data");
    z_stream stream { };
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        throw std::runtime_error("cefast: zlib inflateInit2 failed");
    const detail::inflate_guard guard { &stream };

    std::string output(std::max<std::size_t>(compressed.size() * 4, 4096), '\0');
    std::size_t produced = 0;
    std::size_t consumed = 0;

    for (;;)
    {
        if (stream.avail_in == 0 && consumed < compressed.size())
        {
            const auto chunk = std::min(compressed.size() - consumed, detail::zlib_chunk);
            stream.next_in
                = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data() + consumed));
            stream.avail_in = static_cast<uInt>(chunk);
            consumed += chunk;
        }
        if (produced == output.size())
            output.resize(output.size() * 2);
        const auto room = std::min(output.size() - produced, detail::zlib_chunk);
        stream.next_out = reinterpret_cast<Bytef*>(output.data() + produced);
        stream.avail_out = static_cast<uInt>(room);

        const int status = inflate(&stream, Z_NO_FLUSH);
        const auto written = room - stream.avail_out;
        produced += written;

        if (status == Z_STREAM_END)
        {
            if (stream.avail_in == 0 && consumed >= compressed.size())
                break;
            if (detail::all_zero(stream.next_in, stream.avail_in)
                && detail::all_zero(compressed.data() + consumed, compressed.size() - consumed))
                break;
            if (inflateReset(&stream) != Z_OK)
                throw parse_error(source, 0, "corrupt or truncated gzip data");
            continue;
        }
        if (status != Z_OK && status != Z_BUF_ERROR)
            throw parse_error(source, 0, "corrupt or truncated gzip data");
        if (written == 0 && stream.avail_in == 0 && consumed >= compressed.size())
            throw parse_error(source, 0, "corrupt or truncated gzip data");
    }
    output.resize(produced);
    return output;
}
}
