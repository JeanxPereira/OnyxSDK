#pragma once

#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Onyx::Ui {

struct ProblemRow {
    Modules::DocumentId doc = 0;
    Services::Diag diag;
    std::string text;
    const char* glyph = "";
};

struct ProblemsModel {
    std::vector<ProblemRow> rows;
    size_t info = 0;
    size_t warnings = 0;
    size_t errors = 0;
};

// Lists the diagnostics of every ready document, filtered by severity.
class Problems final : public IPanel {
public:
    std::string_view Id() const override { return "onyx.problems"; }
    std::string_view Title() const override { return "Problems"; }
    void Draw(UiContext& ui) override;

    // The rows only change when a document's diagnostic count or a filter changes.
    const ProblemsModel& Model(UiContext& ui);

    // The deepest entry of the document whose source range contains the byte reference.
    static std::optional<Modules::NodePath> FindEntry(const Modules::Document& doc,
                                                      const Services::ByteRef& at);

    bool showInfo = true;
    bool showWarnings = true;
    bool showErrors = true;

private:
    struct Signature {
        std::vector<std::pair<Modules::DocumentId, size_t>> docs;
        unsigned mask = 0;
        bool operator==(const Signature&) const = default;
    };

    ProblemsModel m_model;
    Signature m_signature;
    bool m_built = false;
};

}
