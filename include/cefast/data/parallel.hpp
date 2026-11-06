#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <cefast/data/records.hpp>
#include <cefast/data/workers.hpp>

namespace cefast
{
namespace detail
{
    // Below this the worker hand-off, variable-list copies and column concatenation cost more than
    // the parse they overlap. Measured in docs/perf/2026-11-round5-parallel.md. Test hook: the fuzz
    // target lowers it so that a small input still takes the parallel path.
    inline std::size_t parallel_threshold = 48 * 1024;

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

    // Everything a chunk parses or writes, kept in one heap object: a worker that has not picked
    // up its job when the load returns still holds this alive, and by then the counter is
    // exhausted so it has nothing left to touch (see run_on_workers).
    struct chunk_work
    {
        std::string_view body;
        std::string_view source;
        std::size_t first_line = 0;
        std::size_t whole_hint = 0;
        std::vector<std::size_t> cuts;
        std::vector<schema> parts;
        std::vector<char> ended_on_marker;
        std::vector<std::exception_ptr> errors;
        std::atomic<std::size_t> next { 0 };
    };

    // A chunk scanner reads a variable's type, sizes, fill token and NRV flag, and nothing else:
    // copying the whole schema per chunk would also copy every global and every attribute (about
    // 1250 allocations per chunk on DWP). The template is built once per load, then copied.
    [[nodiscard]] inline schema chunk_template(const schema& whole)
    {
        schema lean;
        lean.record_marker = whole.record_marker;
        lean.file.variables.reserve(whole.file.variables.size());
        for (const auto& source : whole.file.variables)
        {
            auto& target = lean.file.variables.emplace_back();
            target.type = source.type;
            target.sizes = source.sizes;
            target.is_nrv = source.is_nrv;
            target.fillval_token = source.fillval_token;
        }
        return lean;
    }

    // Cuts the body speculatively, parses the chunks concurrently, then checks that every chunk but
    // the last ended on a real record marker. Chunk 0 starts clean and each verified chunk hands a
    // clean state to the next, so the concatenation is exact. A cut inside a quoted value makes
    // that chunk throw ("unterminated quote") and a cut inside a comment leaves the chunk not
    // ending on a marker, so both discard the parts and reparse the whole body with one scanner:
    // that is why the error message, its line and its record number come from the serial scanner
    // and never depend on the thread count or on where a chunk started.
    inline void parse_records_parallel(schema& result, std::string_view body,
        std::size_t first_line, std::string_view source, std::size_t threads)
    {
        auto state = std::make_shared<chunk_work>();
        state->cuts = speculative_cuts(body, result.record_marker, threads);
        if (state->cuts.empty())
        {
            record_scanner { result, body, first_line, source }.run();
            return;
        }

        state->body = body;
        state->source = source;
        state->first_line = first_line;
        state->whole_hint = records_hint(body);
        const auto chunks = state->cuts.size() + 1;
        state->ended_on_marker.assign(chunks, 0);
        state->errors.resize(chunks);
        state->parts.assign(chunks, chunk_template(result));

        const auto work = [state]
        {
            for (auto index = state->next.fetch_add(1); index < state->parts.size();
                index = state->next.fetch_add(1))
            {
                const auto begin = index == 0 ? 0 : state->cuts[index - 1];
                const auto end
                    = index < state->cuts.size() ? state->cuts[index] : state->body.size();
                try
                {
                    // The chunk's line and record numbers are thrown away with it: the serial
                    // reparse below is what reports an error.
                    record_scanner chunk { state->parts[index],
                        state->body.substr(begin, end - begin), state->first_line, state->source, 0,
                        chunk_records_hint(state->whole_hint, state->body.size(), end - begin) };
                    chunk.run();
                    state->ended_on_marker[index] = chunk.ended_on_marker();
                }
                catch (...)
                {
                    state->errors[index] = std::current_exception();
                }
            }
        };
        run_on_workers(std::min(threads - 1, chunks - 1), work);

        auto& parts = state->parts;
        for (std::size_t index = 0; index < chunks; ++index)
            if (state->errors[index] || (index + 1 < chunks && !state->ended_on_marker[index]))
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
