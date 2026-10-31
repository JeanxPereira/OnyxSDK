#include "Common.h"

#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Services/Paths.h>

#include <ostream>

namespace Onyx::Cli::Detail {

Verb ProbeVerb() {
    return Verb{
        "probe", "<file> [--game <hint>]",
        [](Modules::Workspace& ws, const VerbTable&, std::span<const std::string_view> argv, std::ostream& out, std::ostream& err) {
            Args args;
            if (!ParseArgs(argv, args) || args.positional.empty()) {
                err << "usage: probe <file> [--game <hint>]\n";
                return kUsage;
            }

            const Modules::ProbeRanking ranking = ws.Probe(Services::Paths::FromUtf8(args.positional[0]));
            const Modules::IGameModule* winner = ranking.winner;
            if (!args.game.empty()) winner = ws.FindModule(args.game);

            out << "module\tconfidence\treason\n";
            for (const auto& row : ranking.rows) {
                out << row.module->Info().id << "\t" << row.result.confidence << "\t" << row.result.reason << "\n";
            }
            out << "winner: " << (winner ? winner->Info().id : "none") << "\n";
            return winner ? kOk : kNoModule;
        }};
}

}
