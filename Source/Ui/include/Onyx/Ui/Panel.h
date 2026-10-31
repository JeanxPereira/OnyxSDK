#pragma once

#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Selection.h>

#include <string_view>

namespace Onyx::Ui {

struct UiContext;

class IPanel {
public:
    virtual ~IPanel() = default;

    virtual std::string_view Id() const = 0;
    virtual std::string_view Title() const = 0;
    virtual void Draw(UiContext& ui) = 0;

    bool visible = true;
};

class IDocumentView : public IPanel {
public:
    virtual Modules::DocumentId Document() const = 0;
    virtual Modules::NodePath Node() const = 0;
};

}
