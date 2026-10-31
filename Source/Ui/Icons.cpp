#include "Onyx/Ui/Icons.h"

#include "Onyx/Ui/SfSymbols.h"

namespace Onyx::Ui {

const char* Icon(Types::IconId id) {
    using Types::IconId;
    switch (id) {
    case IconId::None:      return "";
    case IconId::File:      return ICON_SF_DOCUMENT;
    case IconId::Folder:    return ICON_SF_FOLDER_FILL;
    case IconId::Image:     return ICON_SF_PHOTO;
    case IconId::Mesh:      return ICON_SF_CUBE_FILL;
    case IconId::Material:  return ICON_SF_PAINTPALETTE_FILL;
    case IconId::Skeleton:  return ICON_SF_FIGURE_WALK;
    case IconId::Animation: return ICON_SF_FIGURE_RUN;
    case IconId::Audio:     return ICON_SF_WAVEFORM;
    case IconId::Video:     return ICON_SF_PLAY_RECTANGLE_FILL;
    case IconId::Text:      return ICON_SF_TEXT_ALIGNLEFT;
    case IconId::Table:     return ICON_SF_TABLECELLS;
    case IconId::Script:    return ICON_SF_SCROLL_FILL;
    case IconId::Map:       return ICON_SF_MAP_FILL;
    case IconId::Shader:    return ICON_SF_FLAME_FILL;
    case IconId::Container: return ICON_SF_ARCHIVEBOX_FILL;
    case IconId::Raw:       return ICON_SF_DOCUMENT_FILL;
    case IconId::Palette:   return ICON_SF_PAINTPALETTE_FILL;
    case IconId::Warning:   return ICON_SF_EXCLAMATIONMARK_TRIANGLE_FILL;
    case IconId::Error:     return ICON_SF_XMARK_OCTAGON_FILL;
    case IconId::Info:      return ICON_SF_INFO_CIRCLE_FILL;
    case IconId::Search:    return ICON_SF_MAGNIFYINGGLASS;
    case IconId::Settings:  return ICON_SF_GEARSHAPE_FILL;
    case IconId::Play:      return ICON_SF_PLAY_FILL;
    case IconId::Pause:     return ICON_SF_PAUSE_FILL;
    case IconId::Stop:      return ICON_SF_STOP_FILL;
    case IconId::Grid:      return ICON_SF_SQUARE_GRID_3X3;
    case IconId::Bone:      return ICON_SF_FIGURE_STAND;
    case IconId::Camera:    return ICON_SF_VIDEO_FILL;
    case IconId::Export:    return ICON_SF_SQUARE_AND_ARROW_UP;
    }
    return ICON_SF_QUESTIONMARK_CIRCLE;
}

}
