// panels/LayersPanel.hpp
#pragma once

#include "../util/BitBake.hpp"
#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/Label.hpp>

namespace yocto {

class LayersPanel {
public:
    explicit LayersPanel(BitBake& bb);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    void refresh();

private:
    void parse_layers(const std::string& output);
    BitBake& bitbake_;
    std::shared_ptr<tui::Border> panel_;
    std::shared_ptr<tui::ListView> layer_list_;
    std::shared_ptr<tui::Label> status_label_;
};

} // namespace yocto