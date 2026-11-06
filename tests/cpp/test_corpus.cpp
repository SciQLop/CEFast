#include <catch2/catch_test_macros.hpp>

#include <cefast/cefast.hpp>
#include <cefast/data/structural.hpp>
#include <cefast/gzip.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace
{
namespace fs = std::filesystem;

[[nodiscard]] fs::path corpus_dir()
{
    return fs::path { CEFAST_TEST_RESOURCES } / "csa";
}

[[nodiscard]] std::string read_file(const fs::path& path)
{
    std::ifstream stream { path, std::ios::binary };
    REQUIRE(stream);
    return { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> { } };
}

// The !RECORDS= N comment CSA writes right before DATA_UNTIL: read from the raw text so the check
// does not depend on the parser it is checking.
[[nodiscard]] std::size_t records_hint(std::string_view text)
{
    constexpr std::string_view keyword = "!RECORDS=";
    const auto at = text.rfind(keyword);
    REQUIRE(at != std::string_view::npos);
    const auto start = at + keyword.size();
    const auto end = text.find_first_not_of(" 0123456789", start);
    return std::stoul(std::string { text.substr(start, end - start) });
}

[[nodiscard]] const cefast::variable& variable_named(
    const cefast::cef_file& file, std::string_view name)
{
    const auto found = std::ranges::find_if(
        file.variables, [name](const auto& var) { return var.name == name; });
    REQUIRE(found != file.variables.end());
    return *found;
}

template <typename T>
[[nodiscard]] const auto& values_of(const cefast::variable& var)
{
    return std::get<cpp_utils::containers::no_init_vector<T>>(var.values);
}

[[nodiscard]] const auto& strings_of(const cefast::variable& var)
{
    return std::get<std::vector<std::string>>(var.values);
}

// The CSA files are not in the repository: they are fetched with tools/fetch_csa_corpus.py.
// A missing corpus is a skip (meson treats exit code 77 as skipped), never a failure.
// _Exit, not exit: leaving a Catch2 test case through static destructors crashes the XML reporter.
[[noreturn]] void skip_without_corpus()
{
    std::fputs(
        "tests/resources/csa is empty: run `python -I tools/fetch_csa_corpus.py` from the "
        "repository root to download the CSA sample corpus\n",
        stderr);
    std::_Exit(77);
}

[[nodiscard]] std::vector<fs::path> corpus_files()
{
    std::vector<fs::path> files;
    if (fs::is_directory(corpus_dir()))
        for (const auto& entry : fs::directory_iterator { corpus_dir() })
            if (entry.path().extension() == ".cef")
                files.push_back(entry.path());
    if (files.empty())
        skip_without_corpus();
    std::ranges::sort(files);
    return files;
}

// CSA republishes datasets with a new _Vxxxxxx version suffix, so tests find a file by its
// "<DATASET>__" prefix, never by its full name.
[[nodiscard]] fs::path corpus_file(std::string_view dataset)
{
    const std::string prefix = std::string { dataset } + "__";
    for (const auto& path : corpus_files())
        if (path.filename().string().starts_with(prefix))
            return path;
    std::fprintf(stderr,
        "no %s__*.cef in tests/resources/csa: run `python -I "
        "tools/fetch_csa_corpus.py`\n",
        std::string { dataset }.c_str());
    std::_Exit(77);
}

void require_same_scalar(const cefast::scalar& left, const cefast::scalar& right)
{
    REQUIRE(left.index() == right.index());
    std::visit(
        [&right](const auto& value)
        {
            using value_t = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<value_t, cefast::iso_time>)
                REQUIRE(value.ns == std::get<cefast::iso_time>(right).ns);
            else if constexpr (std::is_same_v<value_t, cefast::iso_time_range>)
            {
                const auto& other = std::get<cefast::iso_time_range>(right);
                REQUIRE(value.start == other.start);
                REQUIRE(value.stop == other.stop);
            }
            else
                REQUIRE(value == std::get<value_t>(right));
        },
        left);
}

void require_same_column(const cefast::variable& left, const cefast::variable& right)
{
    REQUIRE(left.type == right.type);
    REQUIRE(left.sizes == right.sizes);
    REQUIRE(left.records == right.records);
    REQUIRE(left.values.index() == right.values.index());
    std::visit(
        [&right](const auto& values)
        {
            const auto& other = std::get<std::decay_t<decltype(values)>>(right.values);
            REQUIRE(values.size() == other.size());
            for (std::size_t index = 0; index < values.size(); ++index)
                REQUIRE(values[index] == other[index]);
        },
        left.values);
}
}

TEST_CASE("corpus: every CSA file loads, with the record count of its !RECORDS hint")
{
    const auto files = corpus_files();
    REQUIRE(files.size() >= 41); // one file per manifest entry
    for (const auto& path : files)
    {
        INFO(path.filename().string());
        const auto expected = records_hint(read_file(path));
        cefast::cef_file file;
        REQUIRE_NOTHROW(file = cefast::load(path));
        REQUIRE_FALSE(file.variables.empty());
        for (const auto& var : file.variables)
            if (!var.is_nrv)
                REQUIRE(var.records == expected);
    }
}

TEST_CASE("corpus: WHI last record matches the file text")
{
    const auto file = cefast::load(corpus_file("C1_CP_WHI_ELECTRON_DENSITY"));

    const auto& time = variable_named(file, "time_tags__C1_CP_WHI_ELECTRON_DENSITY");
    REQUIRE(time.records == 24410);
    // 2002-02-02T11:57:13.320Z
    CHECK(values_of<std::int64_t>(time).back() == 1'012'651'033'320'000'000);

    const auto& delta = variable_named(file, "Delta_Time__C1_CP_WHI_ELECTRON_DENSITY");
    CHECK(values_of<float>(delta).back() == 0.743f);

    const auto& spectrum = variable_named(file, "Spectrum_Type__C1_CP_WHI_ELECTRON_DENSITY");
    CHECK(strings_of(spectrum).back() == "A");

    const auto& quality = variable_named(file, "Quality__C1_CP_WHI_ELECTRON_DENSITY");
    CHECK(values_of<std::int32_t>(quality).back() == 3);
}

TEST_CASE("corpus: CODIF 3d_ions keeps its multidim C layout")
{
    const auto file = cefast::load(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"));

    const auto& ions = variable_named(file, "3d_ions__C1_CP_CIS-CODIF_HS_H1_CS");
    REQUIRE(ions.records == 149);
    REQUIRE(ions.sizes == std::vector<std::size_t> { 8, 16, 31 });
    const auto& values = values_of<float>(ions);
    REQUIRE(values.size() == 149UL * 8 * 16 * 31);
    CHECK(values[0] == 0.0f);
    CHECK(values[30] == -1e31f); // FILLVAL, first record
    CHECK(values[629] == 563.405f); // first non-zero in the first record
    CHECK(values[660] == 563.405f);
    CHECK(values[693] == 613.258f);

    const auto& theta = variable_named(file, "theta__C1_CP_CIS-CODIF_HS_H1_CS");
    REQUIRE(theta.is_nrv);
    REQUIRE(theta.sizes == std::vector<std::size_t> { 8 });
    CHECK(values_of<float>(theta).front() == -78.75f);
    CHECK(values_of<float>(theta).back() == 78.75f);
}

TEST_CASE("corpus: EFW L3 microsecond time tags keep their six digits")
{
    const auto file = cefast::load(corpus_file("C1_CP_EFW_L3_E"));

    const auto& time = variable_named(file, "time_tags__C1_CP_EFW_L3_E");
    REQUIRE(time.records == 300);
    // 2005-06-10T00:00:02.000000Z and 2005-06-10T00:19:58.000000Z
    CHECK(values_of<std::int64_t>(time).front() == 1'118'361'602'000'000'000);
    CHECK(values_of<std::int64_t>(time).back() == 1'118'362'798'000'000'000);

    const auto& vector = variable_named(file, "E_Vec_xy_ISR2__C1_CP_EFW_L3_E");
    REQUIRE(vector.sizes == std::vector<std::size_t> { 2 });
    CHECK(values_of<float>(vector)[0] == 0.112f);
    CHECK(values_of<float>(vector)[1] == -0.407f);

    const auto& sigma = variable_named(file, "E_sigma__C1_CP_EFW_L3_E");
    CHECK(values_of<float>(sigma).front() == 0.178f);

    const auto& bitmask = variable_named(file, "E_bitmask__C1_CP_EFW_L3_E");
    CHECK(values_of<std::int32_t>(bitmask).front() == 4096);
}

TEST_CASE("corpus: sub-millisecond time tags are not rounded away")
{
    const auto file = cefast::load(corpus_file("C3_CP_EDI_EGD"));
    const auto& time = variable_named(file, "time_tags__C3_CP_EDI_EGD");
    REQUIRE(time.records == 13222);
    // 2005-06-10T00:19:53.107151Z
    CHECK(values_of<std::int64_t>(time)[13137] == 1'118'362'793'107'151'000);
}

TEST_CASE("corpus: an ISO_TIME_RANGE variable keeps both ends")
{
    const auto file = cefast::load(corpus_file("C3_CT_AUX_GRMB"));

    const auto& time = variable_named(file, "time_tags__C3_CT_AUX_GRMB");
    REQUIRE(time.type == cefast::value_type::ISO_TIME_RANGE);
    REQUIRE(time.is_nrv == false);
    REQUIRE(time.records == 383);
    const auto& values = values_of<std::int64_t>(time);
    REQUIRE(values.size() == 383UL * 2);
    // 2004-12-31T21:04:12Z/2005-01-01T03:00:00Z
    CHECK(values[0] == 1'104'527'052'000'000'000);
    CHECK(values[1] == 1'104'548'400'000'000'000);
}

TEST_CASE("corpus: a gzipped file loads exactly like its plain version")
{
    const auto plain = read_file(corpus_file("C1_CP_FGM_SPIN"));
    const auto compressed = cefast::gzip(plain);
    REQUIRE(compressed.size() > 2);
    REQUIRE(static_cast<unsigned char>(compressed[0]) == 0x1f);
    REQUIRE(static_cast<unsigned char>(compressed[1]) == 0x8b);

    const auto from_plain = cefast::load(std::span<const char> { plain.data(), plain.size() });
    const auto from_gzip
        = cefast::load(std::span<const char> { compressed.data(), compressed.size() });

    REQUIRE(from_plain.variables.size() == from_gzip.variables.size());
    REQUIRE(from_plain.globals.size() == from_gzip.globals.size());
    for (std::size_t index = 0; index < from_plain.variables.size(); ++index)
    {
        INFO(from_plain.variables[index].name);
        const auto& left = from_plain.variables[index];
        const auto& right = from_gzip.variables[index];
        REQUIRE(left.name == right.name);
        require_same_column(left, right);
    }
    for (std::size_t index = 0; index < from_plain.globals.size(); ++index)
    {
        const auto& left = from_plain.globals[index];
        const auto& right = from_gzip.globals[index];
        REQUIRE(left.name == right.name);
        REQUIRE(left.entries.size() == right.entries.size());
        for (std::size_t entry = 0; entry < left.entries.size(); ++entry)
            require_same_scalar(left.entries[entry], right.entries[entry]);
    }
}

namespace
{
// Runs the parser with the byte-by-byte traversal, then puts the default back even when an
// assertion throws.
struct byte_by_byte_scan
{
    byte_by_byte_scan() { cefast::detail::scan_one_byte_at_a_time() = true; }
    ~byte_by_byte_scan() { cefast::detail::scan_one_byte_at_a_time() = false; }
    byte_by_byte_scan(const byte_by_byte_scan&) = delete;
    byte_by_byte_scan& operator=(const byte_by_byte_scan&) = delete;
};
}

TEST_CASE("corpus: the stop-skipping scan matches the byte-by-byte scan", "[corpus]")
{
    for (const auto& path : corpus_files())
    {
        INFO(path.filename().string());
        const auto skipped = cefast::load(path);
        cefast::cef_file walked;
        {
            const byte_by_byte_scan guard;
            walked = cefast::load(path);
        }
        REQUIRE(skipped.variables.size() == walked.variables.size());
        for (std::size_t index = 0; index < skipped.variables.size(); ++index)
        {
            INFO(skipped.variables[index].name);
            const auto& left = skipped.variables[index];
            const auto& right = walked.variables[index];
            REQUIRE(left.name == right.name);
            REQUIRE(left.records == right.records);
            require_same_column(left, right);
        }
    }
}

namespace
{
[[nodiscard]] cefast::cef_file load_with_threads(const fs::path& path, std::size_t threads)
{
    cefast::load_options options;
    options.threads = threads;
    return cefast::load(path, options);
}

[[nodiscard]] cefast::cef_file load_text_with_threads(std::string_view text, std::size_t threads)
{
    cefast::load_options options;
    options.threads = threads;
    return cefast::load(std::span<const char> { text.data(), text.size() }, options);
}

// Makes even a tiny input take the parallel path, so a test does not need a megabyte of records.
struct forced_parallel
{
    forced_parallel() { cefast::detail::parallel_threshold = 0; }
    ~forced_parallel() { cefast::detail::parallel_threshold = saved; }
    forced_parallel(const forced_parallel&) = delete;
    forced_parallel& operator=(const forced_parallel&) = delete;

private:
    const std::size_t saved = cefast::detail::parallel_threshold;
};

// One dataset's records repeated until the file crosses the parallel threshold, without shipping
// megabytes of test data. The trailer (!RECORDS and the DATA_UNTIL value) is dropped so the copies
// are pure record text.
[[nodiscard]] std::string repeated_records(const fs::path& path, std::size_t copies)
{
    const std::string text = read_file(path);
    const auto data_until = text.rfind("DATA_UNTIL");
    REQUIRE(data_until != std::string::npos);
    const auto body_start = text.find('\n', data_until);
    REQUIRE(body_start != std::string::npos);
    std::string body = text.substr(body_start + 1);
    const auto hint = body.rfind("!RECORDS");
    REQUIRE(hint != std::string::npos);
    body.resize(hint);
    REQUIRE(!body.empty());

    std::string repeated = text.substr(0, body_start + 1);
    for (std::size_t copy = 0; copy < copies; ++copy)
        repeated += body;
    repeated += "END_DATA\n";
    return repeated;
}
}

TEST_CASE("corpus: parallel and serial loads agree on every file")
{
    for (const auto& path : corpus_files())
    {
        INFO(path.filename().string());
        const auto serial = load_with_threads(path, 1);
        const auto parallel = load_with_threads(path, 4);
        REQUIRE(serial.variables.size() == parallel.variables.size());
        for (std::size_t index = 0; index < serial.variables.size(); ++index)
        {
            REQUIRE(serial.variables[index].name == parallel.variables[index].name);
            require_same_column(serial.variables[index], parallel.variables[index]);
        }
    }
}

TEST_CASE("corpus: a parallel parse counts a record that ends at end of file")
{
    // A truncated data section: the last record runs to EOF without a record marker.
    const std::string text = "FILE_FORMAT_VERSION = \"CEF-2.0\"\n"
                             "END_OF_RECORD_MARKER = \"$\"\n"
                             "START_VARIABLE = v\n"
                             " VALUE_TYPE = FLOAT\n"
                             " END_VARIABLE = v\n"
                             "DATA_UNTIL = END_DATA\n"
                             "1.5$\n2.5$\n3.5";
    const forced_parallel force;
    const auto serial = load_text_with_threads(text, 1);
    const auto parallel = load_text_with_threads(text, 4);

    REQUIRE(serial.variables.size() == 1);
    CHECK(serial.variables[0].records == 3);
    CHECK(parallel.variables[0].records == 3);
    require_same_column(serial.variables[0], parallel.variables[0]);
}

// A comment at the end of a record line comments out the rest of that line only: the newline still
// ends the record, so the split must not swallow it as part of the comment.
TEST_CASE("corpus: a comment at the end of a record line still ends the record")
{
    const std::string text = "FILE_FORMAT_VERSION = \"CEF-2.0\"\n"
                             "START_VARIABLE = v\n"
                             " VALUE_TYPE = FLOAT\n"
                             " END_VARIABLE = v\n"
                             "DATA_UNTIL = END_DATA\n"
                             "1.5!\n2.5\n3.5\n";
    const forced_parallel force;
    const auto serial = load_text_with_threads(text, 1);
    const auto parallel = load_text_with_threads(text, 4);

    REQUIRE(serial.variables.size() == 1);
    CHECK(serial.variables[0].records == 3);
    CHECK(parallel.variables[0].records == 3);
    require_same_column(serial.variables[0], parallel.variables[0]);
}

TEST_CASE("corpus: parallel and serial report the same record number after a comment")
{
    const std::string text = "FILE_FORMAT_VERSION = \"CEF-2.0\"\n"
                             "START_VARIABLE = v\n"
                             " VALUE_TYPE = FLOAT\n"
                             " END_VARIABLE = v\n"
                             "DATA_UNTIL = END_DATA\n"
                             "1.5!\n2.5\nx3.5\n";
    const forced_parallel force;
    std::string serial_error;
    std::string parallel_error;
    try
    {
        std::ignore = load_text_with_threads(text, 1);
    }
    catch (const cefast::parse_error& error)
    {
        serial_error = error.what();
    }
    try
    {
        std::ignore = load_text_with_threads(text, 4);
    }
    catch (const cefast::parse_error& error)
    {
        parallel_error = error.what();
    }
    REQUIRE(!serial_error.empty());
    CHECK(serial_error.find("(record 3)") != std::string::npos);
    CHECK(parallel_error == serial_error);
}

TEST_CASE("corpus: parallel parsing agrees with serial on a synthetic large file")
{
    constexpr std::size_t copies = 20;
    const auto text = repeated_records(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"), copies);
    REQUIRE(text.size() > cefast::detail::parallel_threshold);

    const auto serial = load_text_with_threads(text, 1);
    const auto parallel = load_text_with_threads(text, 4);
    const auto automatic = load_text_with_threads(text, 0);

    REQUIRE(serial.variables.size() == parallel.variables.size());
    for (std::size_t index = 0; index < serial.variables.size(); ++index)
    {
        require_same_column(serial.variables[index], parallel.variables[index]);
        require_same_column(serial.variables[index], automatic.variables[index]);
    }
    const auto& ions = variable_named(serial, "3d_ions__C1_CP_CIS-CODIF_HS_H1_CS");
    CHECK(ions.records == 149 * copies);
}

TEST_CASE("corpus: a parallel parse reports the first error in file order")
{
    std::string text = repeated_records(corpus_file("C1_CP_CIS-CODIF_HS_H1_CS"), 20);
    // One bad value in the first record and one near the end: the early one must win.
    const auto first = text.find(", 8.031,");
    REQUIRE(first != std::string::npos);
    text.replace(first, 8, ", x8.031");
    const auto late = text.rfind(", 15462.2,");
    REQUIRE(late != std::string::npos);
    text.replace(late, 10, ", y15462.2");

    std::string serial_error;
    std::string parallel_error;
    try
    {
        std::ignore = load_text_with_threads(text, 1);
    }
    catch (const cefast::parse_error& error)
    {
        serial_error = error.what();
    }
    try
    {
        std::ignore = load_text_with_threads(text, 4);
    }
    catch (const cefast::parse_error& error)
    {
        parallel_error = error.what();
    }
    REQUIRE(!serial_error.empty());
    CHECK(parallel_error == serial_error);
    CHECK(serial_error.find("x8.031") != std::string::npos);
}

namespace
{
// Puts the general ISO 8601 path back for the lifetime of the guard.
struct general_time_path
{
    general_time_path() { cefast::detail::fixed_time_path() = false; }
    ~general_time_path() { cefast::detail::fixed_time_path() = true; }
    general_time_path(const general_time_path&) = delete;
    general_time_path& operator=(const general_time_path&) = delete;
};
}

TEST_CASE("corpus: the fixed-width time fast path matches the general path", "[corpus]")
{
    for (const auto& path : corpus_files())
    {
        INFO(path.filename().string());
        const auto fast = cefast::load(path);
        cefast::cef_file general;
        {
            const general_time_path guard;
            general = cefast::load(path);
        }
        REQUIRE(fast.variables.size() == general.variables.size());
        for (std::size_t index = 0; index < fast.variables.size(); ++index)
        {
            INFO(fast.variables[index].name);
            const auto& left = fast.variables[index];
            const auto& right = general.variables[index];
            REQUIRE(left.name == right.name);
            REQUIRE(left.records == right.records);
            require_same_column(left, right);
        }
    }
}

namespace
{
// A file whose data section is exactly `data`, with `marker` as the record marker. The tests below
// place markers inside quotes and comments at the byte offsets a speculative split cuts at.
[[nodiscard]] std::string file_with_data(
    std::string_view marker, std::string_view variable, std::string_view data)
{
    std::string text { "FILE_FORMAT_VERSION = \"CEF-2.0\"\nEND_OF_RECORD_MARKER = " };
    text += marker;
    text += '\n';
    text += variable;
    text += "\nDATA_UNTIL = END_DATA\n";
    text += data;
    text += "END_DATA\n";
    return text;
}

constexpr std::string_view one_float_variable = R"(START_VARIABLE = v
  VALUE_TYPE = FLOAT
END_VARIABLE = v)";

constexpr std::string_view one_char_variable = R"(START_VARIABLE = c
  VALUE_TYPE = CHAR
END_VARIABLE = c)";

// Loads `text` serially and with four threads, requires the two results to agree on every column
// and returns the parallel one.
[[nodiscard]] cefast::cef_file require_parallel_matches_serial(std::string_view text)
{
    const auto serial = load_text_with_threads(text, 1);
    auto parallel = load_text_with_threads(text, 4);
    REQUIRE(serial.variables.size() == parallel.variables.size());
    for (std::size_t index = 0; index < serial.variables.size(); ++index)
    {
        INFO(serial.variables[index].name);
        require_same_column(serial.variables[index], parallel.variables[index]);
    }
    return parallel;
}
}

// A quoted CHAR value holds markers, so the raw text search finds one at every cut offset and the
// chunk that ends there stops inside an unterminated quote. The value also spans lines, so the
// quote a chunk starts in is not closed anywhere in that chunk. Verification must reject the
// speculative chunks and fall back to the serial scanner.
TEST_CASE("corpus: a cut inside a quoted value falls back")
{
    std::string inner;
    for (std::size_t unit = 0; unit < 4096; ++unit)
        inner += "0123456789abcde$\n";
    // Body = 69636 bytes: the cuts at size/4, size/2 and 3*size/4 (17409, 34818, 52227) are all
    // inside the quote, which opens at 0 and closes at 69633.
    const auto text = file_with_data("$", one_char_variable, "\"" + inner + "\"$\n");

    const forced_parallel force;
    const auto parallel = require_parallel_matches_serial(text);
    REQUIRE(parallel.variables.size() == 1);
    CHECK(parallel.variables[0].records == 1);
    CHECK(strings_of(parallel.variables[0]).front() == inner);
}

// A '!' comment runs to the end of its line, so a marker inside it is data, not a record end. When
// a cut lands on such a marker the chunk ends inside a comment that never terminates: the scanner
// does not end on a marker and the parse falls back to the serial scanner.
TEST_CASE("corpus: a cut inside a comment falls back")
{
    std::string body = "1.5$\n";
    for (std::size_t line = 0; line < 4096; ++line)
        body += "!" + std::string(30, '$') + "\n";
    // Body = 131077 bytes; the cuts at 32769, 65538 and 98307 all land on a '$' inside a comment.
    const auto text = file_with_data("$", one_float_variable, body);

    const forced_parallel force;
    const auto parallel = require_parallel_matches_serial(text);
    REQUIRE(parallel.variables.size() == 1);
    CHECK(parallel.variables[0].records == 1);
    CHECK(values_of<float>(parallel.variables[0]).front() == 1.5f);
}

// The error a chunk raises is discarded: the whole body is reparsed with one scanner, so the line
// and record number are the serial ones even when the bad value sits in the last chunk.
TEST_CASE("corpus: an error in a late chunk keeps the serial message")
{
    std::string body;
    for (std::size_t record = 0; record < 4000; ++record)
        body += record == 3200 ? "x1.5$\n" : "1.5$\n";
    const auto text = file_with_data("$", one_float_variable, body);

    const forced_parallel force;
    std::string serial_error;
    std::string parallel_error;
    try
    {
        std::ignore = load_text_with_threads(text, 1);
    }
    catch (const cefast::parse_error& error)
    {
        serial_error = error.what();
    }
    try
    {
        std::ignore = load_text_with_threads(text, 4);
    }
    catch (const cefast::parse_error& error)
    {
        parallel_error = error.what();
    }
    REQUIRE(!serial_error.empty());
    CHECK(serial_error.find("(record 3201)") != std::string::npos);
    CHECK(parallel_error == serial_error);
}

// The cut search is text based, so a multi-byte marker goes through find() and the same verify and
// fall back rules; nothing may assume a one-byte marker.
TEST_CASE("corpus: a multi-character marker splits and parses like serial")
{
    std::string body;
    for (std::size_t record = 0; record < 4000; ++record)
        body += "1.5$$\n";
    const auto text = file_with_data("\"$$\"", one_float_variable, body);

    const forced_parallel force;
    const auto parallel = require_parallel_matches_serial(text);
    REQUIRE(parallel.variables.size() == 1);
    CHECK(parallel.variables[0].records == 4000);
    CHECK(values_of<float>(parallel.variables[0]).back() == 1.5f);
}

// The cuts are the contract between the split and the verifier: each one is right after a marker,
// strictly after the previous one (so no chunk is empty) and inside the body (so there is always a
// last chunk). A body without a marker yields no cuts at all and the caller parses it serially.
TEST_CASE("parallel: speculative cuts land right after a marker")
{
    std::string body;
    for (std::size_t record = 0; record < 100; ++record)
        body += "1.5$\n";
    const auto cuts = cefast::detail::speculative_cuts(body, "$", 4);
    REQUIRE(cuts.size() == 3);
    for (std::size_t index = 0; index < cuts.size(); ++index)
    {
        REQUIRE(cuts[index] > 0);
        REQUIRE(cuts[index] < body.size());
        CHECK(body[cuts[index] - 1] == '$');
        if (index > 0)
            CHECK(cuts[index] > cuts[index - 1]);
    }

    // The same marker found from several offsets is one cut, not an empty chunk.
    const auto deduped = cefast::detail::speculative_cuts("1234$$5678", "$$", 8);
    REQUIRE(deduped.size() == 1);
    CHECK(deduped.front() == 6);

    CHECK(cefast::detail::speculative_cuts("1.5\n2.5\n", "$", 4).empty());
    CHECK(cefast::detail::speculative_cuts("1.5$\n2.5$", "$", 1).empty());
    CHECK(cefast::detail::speculative_cuts("1.5$\n2.5$", "", 4).empty());
}

// Two loads in flight at once share one pool of persistent workers: neither may wait for the
// other's jobs to drain, and both must produce exactly the serial columns.
TEST_CASE("parallel: two concurrent loads share the workers")
{
    std::string body;
    for (std::size_t record = 0; record < 2000; ++record)
        body += "1.5$\n";
    const auto text = file_with_data("$", one_float_variable, body);
    const auto serial = load_text_with_threads(text, 1);

    const forced_parallel force;
    constexpr std::size_t iterations = 50;
    cefast::cef_file last[2];
    std::vector<std::thread> loaders;
    for (std::size_t slot = 0; slot < 2; ++slot)
        loaders.emplace_back(
            [&, slot]
            {
                for (std::size_t iteration = 0; iteration < iterations; ++iteration)
                    last[slot] = load_text_with_threads(text, 4);
            });
    for (auto& loader : loaders)
        loader.join();

    REQUIRE(serial.variables.size() == 1);
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        REQUIRE(last[slot].variables.size() == serial.variables.size());
        CHECK(last[slot].variables[0].records == 2000);
        require_same_column(serial.variables[0], last[slot].variables[0]);
    }
}

// Far more threads than chunks: most jobs find the shared counter already exhausted and finish
// without touching anything. A load must still return with every chunk parsed, and a job that
// wakes after the caller is done must not see freed state (this is the ASan-suite case).
TEST_CASE("parallel: load returns before a slow worker")
{
    std::string body;
    for (std::size_t record = 0; record < 4; ++record)
    {
        body += "1.5" + std::string(59, ' ');
        if (record + 1 < 4)
            body += "$";
    }
    // The terminator is found as the text starting a line, so the last record needs its newline.
    body += '\n';
    const auto text = file_with_data("$", one_float_variable, body);
    // The speculative split finds three markers in a body this long, so four chunks are parsed by
    // the worker copies that all race for them.
    REQUIRE(cefast::detail::speculative_cuts(body, "$", 64).size() == 3);
    const auto serial = load_text_with_threads(text, 1);
    const forced_parallel force;
    for (std::size_t iteration = 0; iteration < 500; ++iteration)
    {
        const auto parallel = load_text_with_threads(text, 64);
        REQUIRE(parallel.variables[0].records == 4);
        require_same_column(serial.variables[0], parallel.variables[0]);
    }
}
