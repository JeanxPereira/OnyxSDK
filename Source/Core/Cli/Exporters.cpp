#include <Onyx/Core/Cli/Exporters.h>

namespace Onyx::Cli {

void Exporters::Add(std::string format, Domain::MediaKind kind, ExportFn write) {
    Exporter exporter{format, kind, std::move(write)};
    m_byFormat.insert_or_assign(std::move(format), std::move(exporter));
}

const Exporter* Exporters::Find(std::string_view format) const {
    const auto it = m_byFormat.find(format);
    return it == m_byFormat.end() ? nullptr : &it->second;
}

}
