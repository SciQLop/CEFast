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
