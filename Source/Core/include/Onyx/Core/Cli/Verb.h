#pragma once

#include <Onyx/Core/Cli/Exporters.h>
#include <Onyx/Core/Modules/Workspace.h>

#include <functional>
#include <iosfwd>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Cli {

struct Verb {
    std::string name;
    std::string usage;
    std::function<int(Modules::Workspace&, std::span<const std::string_view> args, std::ostream& out,
                      std::ostream& err)>
        run;
};

class VerbTable {
public:
    // Adding a verb with an existing name replaces it.
    void Add(Verb verb);
    void Add(std::vector<Verb> verbs);

    const Verb* Find(std::string_view name) const;
    // Sorted by name.
    std::vector<std::string_view> Names() const;

    Cli::Exporters& Exporters() { return m_exporters; }
    const Cli::Exporters& Exporters() const { return m_exporters; }

private:
    std::map<std::string, Verb, std::less<>> m_verbs;
    Cli::Exporters m_exporters;
};

}
