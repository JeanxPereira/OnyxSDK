#include <Onyx/Core/Cli/Run.h>

#include <ostream>

namespace Onyx::Cli {

void VerbTable::Add(Verb verb) {
    std::string name = verb.name;
    m_verbs.insert_or_assign(std::move(name), std::move(verb));
}

void VerbTable::Add(std::vector<Verb> verbs) {
    for (Verb& v : verbs) Add(std::move(v));
}

const Verb* VerbTable::Find(std::string_view name) const {
    const auto it = m_verbs.find(name);
    return it == m_verbs.end() ? nullptr : &it->second;
}

std::vector<std::string_view> VerbTable::Names() const {
    std::vector<std::string_view> names;
    names.reserve(m_verbs.size());
    for (const auto& [name, verb] : m_verbs) names.push_back(name);
    return names;
}

namespace {

void PrintUsage(const VerbTable& table, std::string_view program, std::ostream& err) {
    err << "usage: " << program << " <verb> [args]\n";
    for (std::string_view name : table.Names()) {
        err << "  " << name << ' ' << table.Find(name)->usage << '\n';
    }
}

}

int Run(Modules::Workspace& ws, const VerbTable& table, int argc, char** argv, std::ostream& out,
        std::ostream& err) {
    const std::string_view program = argc > 0 ? argv[0] : "onyx";
    if (argc < 2) {
        PrintUsage(table, program, err);
        return kUsage;
    }

    const Verb* verb = table.Find(argv[1]);
    if (!verb || !verb->run) {
        err << "unknown verb: " << argv[1] << '\n';
        PrintUsage(table, program, err);
        return kUsage;
    }

    std::vector<std::string_view> args;
    for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
    return verb->run(ws, table, args, out, err);
}

}
