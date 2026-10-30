#include <doctest/doctest.h>

#include <Onyx/Core/Cli/Exporters.h>
#include <Onyx/Core/Cli/Json.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Diagnostics.h>

#include <sstream>
#include <string>
#include <vector>

using namespace Onyx;

namespace {

struct Argv {
    std::vector<std::string> owned;
    std::vector<char*> ptrs;
    explicit Argv(std::vector<std::string> args) : owned(std::move(args)) {
        for (std::string& s : owned) ptrs.push_back(s.data());
    }
    int Argc() const { return static_cast<int>(ptrs.size()); }
    char** Data() { return ptrs.data(); }
};

Cli::Verb Echo(std::string name, int rc) {
    return Cli::Verb{std::move(name), "<file>",
                     [rc](Modules::Workspace&, std::span<const std::string_view> args, std::ostream& out,
                          std::ostream&) {
                         for (std::string_view a : args) out << a << ";";
                         return rc;
                     }};
}

}

TEST_CASE("CliRun.NoArgsPrintsUsageAndReturnsKUsage") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Echo("beta", 0));
    Argv argv({"onyx-tool"});
    std::ostringstream out, err;
    CHECK(Cli::Run(ws, table, argv.Argc(), argv.Data(), out, err) == Cli::kUsage);
    CHECK(err.str().find("onyx-tool") != std::string::npos);
    CHECK(err.str().find("beta") != std::string::npos);
    CHECK(out.str().empty());
}

TEST_CASE("CliRun.UnknownVerbListsVerbs") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add({Echo("zeta", 0), Echo("alpha", 0)});
    Argv argv({"tool", "frobnicate"});
    std::ostringstream out, err;
    CHECK(Cli::Run(ws, table, argv.Argc(), argv.Data(), out, err) == Cli::kUsage);
    const std::string text = err.str();
    const size_t a = text.find("alpha");
    const size_t z = text.find("zeta");
    REQUIRE(a != std::string::npos);
    REQUIRE(z != std::string::npos);
    CHECK(a < z);
    CHECK(text.find("frobnicate") != std::string::npos);
}

TEST_CASE("CliRun.DispatchesByName") {
    Modules::Workspace ws;
    Cli::VerbTable table;
    table.Add(Echo("one", 0));
    table.Add(Echo("two", Cli::kPartial));
    CHECK(table.Find("three") == nullptr);
    REQUIRE(table.Find("two") != nullptr);
    CHECK(table.Names() == std::vector<std::string_view>{"one", "two"});

    Argv argv({"tool", "two", "x", "--flag"});
    std::ostringstream out, err;
    CHECK(Cli::Run(ws, table, argv.Argc(), argv.Data(), out, err) == Cli::kPartial);
    CHECK(out.str() == "x;--flag;");
}

TEST_CASE("CliRun.ExportersFindByFormat") {
    Cli::VerbTable table;
    int calls = 0;
    table.Exporters().Add("gltf", Domain::MediaKind::Mesh, [&calls](const void*, const std::filesystem::path&) {
        ++calls;
        return Result<void>::Ok();
    });
    const Cli::Exporter* found = table.Exporters().Find("gltf");
    REQUIRE(found != nullptr);
    CHECK(found->kind == Domain::MediaKind::Mesh);
    CHECK(found->write(nullptr, "unused").ok());
    CHECK(calls == 1);
    CHECK(table.Exporters().Find("png") == nullptr);
}

TEST_CASE("CliRun.JsonEscapeControlChars") {
    CHECK(Cli::JsonEscape("a\"b\\c\n\r\t") == "a\\\"b\\\\c\\n\\r\\t");
    CHECK(Cli::JsonEscape(std::string_view("\x01\x1f", 2)) == "\\u0001\\u001f");
    CHECK(Cli::JsonEscape("plain") == "plain");

    Services::DiagSink sink;
    sink.Report({Services::Severity::Warning, "x.code", "message", std::nullopt});
    std::ostringstream out;
    Cli::PrintDiags(sink, out);
    CHECK(out.str() == "[Warning] x.code: message\n");
}
