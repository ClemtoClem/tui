/* @file Gallery.hpp — construction de la galerie libtui (testable sans terminal). */
#pragma once

#include <tui/Tui.hpp>

#include <memory>

class Gallery {
public:
    explicit Gallery(tui::App& app);
    /// Une itération d'animation (appelée toutes les 100 ms).
    void tick();

    tui::App& app_;
    bool default_css_ = true;
    int tick_ = 0;
    std::shared_ptr<tui::Label> status;
    std::shared_ptr<tui::Header> header;
    std::shared_ptr<tui::ListView> themes_list;
    std::shared_ptr<tui::Border> sidebar;
    std::shared_ptr<tui::Vertical> controls;
    std::shared_ptr<tui::Horizontal> buttons;
    std::shared_ptr<tui::Input> input;
    std::shared_ptr<tui::Checkbox> check;
    std::shared_ptr<tui::ProgressBar> progress;
    std::shared_ptr<tui::Link> link;
    std::shared_ptr<tui::Vertical> data;
    std::shared_ptr<tui::Digits> clock;
    std::shared_ptr<tui::Sparkline> spark;
    std::shared_ptr<tui::LoadingIndicator> loading;
    std::shared_ptr<tui::Log> log;
    std::shared_ptr<tui::Vertical> layout;
    std::shared_ptr<tui::Tabs> tabs;
    std::shared_ptr<tui::Horizontal> body;
    std::shared_ptr<tui::Footer> footer;
    std::shared_ptr<tui::Vertical> root;
};
