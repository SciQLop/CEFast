#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <cefast/data/records.hpp>

namespace cefast
{
namespace detail
{
    // Below this the thread start-up costs more than the parse.
    inline constexpr std::size_t parallel_threshold = 512 * 1024;

    inline std::size_t default_threads() noexcept
    {
        const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
        return std::clamp<std::size_t>(hardware, 1, 8);
    }

    struct record_chunk
    {
        std::size_t begin;
        std::size_t end;
        std::size_t first_line;
        std::size_t first_record;
    };

    // Cuts the data section into chunks of whole records. This walks the same rules as
    // record_scanner::run (a quote opens a value, '!' comments out the rest of the line, the marker
    // ends a record) but parses nothing: it only counts markers, so the chunks can be parsed
    // concurrently. A marker with no value before it is not a record, exactly like end_record().
    [[nodiscard]] inline std::vector<record_chunk> split_records(std::string_view body,
        std::string_view marker, std::size_t first_line, std::size_t chunk_bytes,
        std::size_t& total_records)
    {
        std::vector<record_chunk> chunks;
        total_records = 0;
        if (body.empty() || chunk_bytes == 0)
            return chunks;

        const bool single = marker.size() == 1;
        const char marker_char = marker.empty() ? '\0' : marker.front();
        std::size_t line = first_line;
        std::size_t records = 0;
        std::size_t chunk_begin = 0;
        std::size_t chunk_line = first_line;
        std::size_t chunk_record = 0;
        bool content = false;
        bool quoted = false;
        bool comment = false;

        for (std::size_t position = 0; position < body.size();)
        {
            const char c = body[position];
            if (comment)
            {
                if (c == '\n')
                {
                    comment = false;
                    ++line;
                }
                ++position;
                continue;
            }
            if (quoted)
            {
                if (c == '"')
                    quoted = false;
                ++position;
                continue;
            }
            if (c == '"')
            {
                quoted = true;
                content = true;
                ++position;
                continue;
            }
            if (c == '!')
            {
                comment = true;
                ++position;
                continue;
            }
            const bool at_marker = !marker.empty()
                && (single ? c == marker_char : body.compare(position, marker.size(), marker) == 0);
            if (at_marker)
            {
                if (content)
                    ++records;
                content = false;
                position += marker.size();
                if (marker == "\n")
                    ++line;
                if (position - chunk_begin >= chunk_bytes)
                {
                    chunks.push_back({ chunk_begin, position, chunk_line, chunk_record });
                    chunk_begin = position;
                    chunk_line = line;
                    chunk_record = records;
                }
                continue;
            }
            if (c == '\n')
                ++line;
            else if (c != ' ' && c != '\t' && c != '\r')
                content = true;
            ++position;
        }
        if (chunk_begin < body.size() || chunks.empty())
            chunks.push_back({ chunk_begin, body.size(), chunk_line, chunk_record });
        total_records = records;
        return chunks;
    }

    // Moves every value of `source` (same alternative, same variable) to the end of `destination`.
    inline void append_column(column& destination, column& source)
    {
        std::visit(
            [&source](auto& target)
            {
                using vector_type = std::decay_t<decltype(target)>;
                auto& origin = std::get<vector_type>(source);
                if constexpr (std::is_same_v<typename vector_type::value_type, std::string>)
                {
                    for (auto& value : origin)
                        target.push_back(std::move(value));
                }
                else
                    target.insert(target.end(), origin.begin(), origin.end());
            },
            destination);
    }

    // One record_scanner per chunk, each on its own copy of the header, then the columns are
    // concatenated in file order. Errors are kept per chunk and the earliest one (file order) is
    // rethrown, so the message does not depend on which thread finished first.
    inline void parse_records_parallel(schema& result, std::string_view body,
        std::size_t first_line, std::string_view source, std::size_t threads)
    {
        std::size_t total_records = 0;
        const auto chunks = split_records(
            body, result.record_marker, first_line, body.size() / threads + 1, total_records);
        if (chunks.size() <= 1)
        {
            record_scanner { result, body, first_line, source }.run();
            return;
        }

        std::vector<schema> parts(chunks.size(), result);
        std::vector<std::exception_ptr> errors(chunks.size());
        std::atomic<std::size_t> next { 0 };
        const auto work = [&]
        {
            for (auto index = next.fetch_add(1); index < chunks.size(); index = next.fetch_add(1))
            {
                const auto& chunk = chunks[index];
                const auto last
                    = index + 1 < chunks.size() ? chunks[index + 1].first_record : total_records;
                const auto count = last > chunk.first_record ? last - chunk.first_record : 0;
                try
                {
                    record_scanner {
                        parts[index], body.substr(chunk.begin, chunk.end - chunk.begin),
                        chunk.first_line, source, chunk.first_record,
                        std::optional<std::size_t> { count }
                    }.run();
                }
                catch (...)
                {
                    errors[index] = std::current_exception();
                }
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(threads - 1);
        for (std::size_t started = 1; started < threads; ++started)
            pool.emplace_back(work);
        work();
        for (auto& worker : pool)
            worker.join();

        for (const auto& error : errors)
            if (error)
                std::rethrow_exception(error);

        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            auto& part = parts[index].file.variables;
            auto& whole = result.file.variables;
            for (std::size_t variable = 0; variable < whole.size(); ++variable)
            {
                if (whole[variable].is_nrv)
                    continue;
                // The schema's own columns are still default (untyped): take the first chunk's.
                if (index == 0)
                    whole[variable].values = std::move(part[variable].values);
                else
                    append_column(whole[variable].values, part[variable].values);
            }
        }
        for (auto& variable : result.file.variables)
            if (!variable.is_nrv)
                variable.records = total_records;
    }
}
}
