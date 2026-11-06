#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
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
    // Below this the thread start-up, variable-list copies and column concatenation cost more than
    // the parse they overlap. Measured at a ~190 KiB body break-even, see
    // docs/perf/2026-11-round5-parallel.md. Test hook: the fuzz target lowers it so that a small
    // input still takes the parallel path.
    inline std::size_t parallel_threshold = 256 * 1024;

    inline std::size_t default_threads() noexcept
    {
        const auto hardware = static_cast<std::size_t>(std::thread::hardware_concurrency());
        return std::clamp<std::size_t>(hardware, 1, 8);
    }

    // Cut points for the speculative split: for k = 1..parts-1 look at byte k*size/parts and cut
    // right after the next record marker, if any. This is a raw text search, so a cut can land
    // inside a quoted value or a comment; parse_records_parallel verifies the chunks and falls back
    // to one serial scanner when they do not line up. Each cut is strictly after the previous one,
    // so no chunk is empty and none is parsed twice, and it is strictly inside the body, so there is
    // always a last chunk. No marker (or a single part) means no cuts at all.
    [[nodiscard]] inline std::vector<std::size_t> speculative_cuts(
        std::string_view body, std::string_view marker, std::size_t parts)
    {
        std::vector<std::size_t> cuts;
        if (body.empty() || marker.empty() || parts < 2)
            return cuts;
        cuts.reserve(parts - 1);
        const auto step = body.size() / parts;
        for (std::size_t part = 1; part < parts; ++part)
        {
            const auto at = body.find(marker, step * part);
            if (at == std::string_view::npos)
                break;
            const auto cut = at + marker.size();
            if (cut >= body.size())
                break;
            if (cuts.empty() || cut > cuts.back())
                cuts.push_back(cut);
        }
        return cuts;
    }

    // A chunk's share of the whole body's !RECORDS hint, rounded up: enough to pre-size the chunk's
    // columns so none has to grow mid-parse. The hint is a reservation only, so a share rounded up
    // by one costs nothing and an exact split is not needed.
    [[nodiscard]] inline std::optional<std::size_t> chunk_records_hint(
        std::size_t whole_hint, std::size_t body_bytes, std::size_t chunk_bytes) noexcept
    {
        if (whole_hint == 0 || body_bytes == 0)
            return std::nullopt;
        const auto share = static_cast<double>(whole_hint)
            * (static_cast<double>(chunk_bytes) / static_cast<double>(body_bytes));
        return static_cast<std::size_t>(std::ceil(share));
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

    // Cuts the body speculatively, parses the chunks concurrently, then checks that every chunk but
    // the last ended on a real record marker. Chunk 0 starts clean and each verified chunk hands a
    // clean state to the next, so the concatenation is exact. A cut inside a quoted value makes that
    // chunk throw ("unterminated quote") and a cut inside a comment leaves the chunk not ending on a
    // marker, so both discard the parts and reparse the whole body with one scanner: that is why the
    // error message, its line and its record number come from the serial scanner and never depend on
    // the thread count or on where a chunk started.
    inline void parse_records_parallel(schema& result, std::string_view body,
        std::size_t first_line, std::string_view source, std::size_t threads)
    {
        const auto cuts = speculative_cuts(body, result.record_marker, threads);
        if (cuts.empty())
        {
            record_scanner { result, body, first_line, source }.run();
            return;
        }

        const auto chunks = cuts.size() + 1;
        const auto chunk_begin = [&cuts](std::size_t index)
        { return index == 0 ? 0 : cuts[index - 1]; };
        const auto whole_hint = records_hint(body);
        std::vector<schema> parts(chunks, result);
        std::vector<char> ended_on_marker(chunks, 0);
        std::vector<std::exception_ptr> errors(chunks);
        std::atomic<std::size_t> next { 0 };
        const auto work = [&]
        {
            for (auto index = next.fetch_add(1); index < chunks; index = next.fetch_add(1))
            {
                const auto begin = chunk_begin(index);
                const auto end = index < cuts.size() ? cuts[index] : body.size();
                try
                {
                    // The chunk's line and record numbers are thrown away with it: the serial
                    // reparse below is what reports an error.
                    record_scanner chunk { parts[index], body.substr(begin, end - begin), first_line,
                        source, 0, chunk_records_hint(whole_hint, body.size(), end - begin) };
                    chunk.run();
                    ended_on_marker[index] = chunk.ended_on_marker();
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

        for (std::size_t index = 0; index < chunks; ++index)
            if (errors[index] || (index + 1 < chunks && !ended_on_marker[index]))
            {
                // The parts are copies, so the serial scanner sees the pristine schema and produces
                // the message, line and record numbers of the serial path by construction.
                record_scanner { result, body, first_line, source }.run();
                return;
            }

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

        // Every varying variable of a chunk counts the same records: one per chunk is the total.
        std::size_t total_records = 0;
        for (const auto& part : parts)
            for (const auto& variable : part.file.variables)
                if (!variable.is_nrv)
                {
                    total_records += variable.records;
                    break;
                }
        for (auto& variable : result.file.variables)
            if (!variable.is_nrv)
                variable.records = total_records;
    }
}
}
