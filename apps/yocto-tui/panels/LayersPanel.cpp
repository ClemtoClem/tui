// panels/LayersPanel.cpp
#include "LayersPanel.hpp"
#include "../Theme.hpp"
#include <tui/widget/layout/Vertical.hpp>

namespace yocto {

LayersPanel::LayersPanel(BitBake& bb) : bitbake_(bb) {
    auto title = std::make_shared<tui::Label>("Layers du projet");
    title->set_style(tui::TextStyle::Bold);

    layer_list_ = std::make_shared<tui::ListView>();
    layer_list_->set_style(tui::ListViewStyle::Detailed);
    layer_list_->set_colors(kAccent, kFg, kMuted);

    status_label_ = std::make_shared<tui::Label>("Chargement…");
    status_label_->set_foreground(kMuted);

    auto content = std::make_shared<tui::Vertical>();
    content->add_child(title, tui::LayoutParams::fixed(1));
    content->add_child(status_label_, tui::LayoutParams::fixed(1));
    content->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
    content->add_child(layer_list_, tui::LayoutParams::stretch(1));

    panel_ = std::make_shared<tui::Border>();
    panel_->set_border_style(tui::BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_title("Layers");
    panel_->set_child(content);
}

void LayersPanel::refresh() {
    auto result = bitbake_.list_layers();
    parse_layers(result.output);
}

void LayersPanel::parse_layers(const std::string& output) {
    std::vector<tui::ListItem> items;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        // Format : BBLAYERS="... /path/to/meta-xxx ..."
        if (line.find("BBLAYERS") != std::string::npos) {
            size_t start = line.find('"');
            size_t end = line.rfind('"');
            if (start != std::string::npos && end != std::string::npos && end > start) {
                std::string layers = line.substr(start + 1, end - start - 1);
                std::istringstream lss(layers);
                std::string layer;
                while (lss >> layer) {
                    items.push_back({layer, "", true, "📦"});
                }
            }
        }
    }
    layer_list_->set_items(std::move(items));
    status_label_->set_text(std::to_string(layer_list_->items().size()) + " layers détectés");
}

} // namespace yocto