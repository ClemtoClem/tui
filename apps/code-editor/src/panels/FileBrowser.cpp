#include "panels/FileBrowser.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <filesystem>

namespace ide {

namespace {

namespace fs = std::filesystem;

class QuitTreeView : public tui::TreeView {
public:
    void set_on_quit(std::function<void()> cb) { on_quit_ = std::move(cb); }

    bool on_key(const tui::KeyEvent& e) override {
        if (e.key == tui::Key::Char && (e.codepoint == U'q' || e.codepoint == U'Q')) {
            if (on_quit_) on_quit_();
            return true;
        }
        return tui::TreeView::on_key(e);
    }

private:
    std::function<void()> on_quit_;
};

void populate_children(tui::TreeNode& node, const std::string& dir_path) {
    node.children.clear();

    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec)) {
        entries.push_back(e);
    }
    if (ec) return; // répertoire illisible : reste vide plutôt que de planter

    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        bool ad = a.is_directory(), bd = b.is_directory();
        if (ad != bd) return ad; // répertoires avant fichiers
        return a.path().filename().string() < b.path().filename().string();
    });

    for (auto& e : entries) {
        auto child = std::make_shared<tui::TreeNode>();
        child->label = e.path().filename().string();
        child->id = e.path().string();
        if (e.is_directory()) {
            std::string child_path = child->id;
            child->lazy_expand = [child_path](tui::TreeNode& n) { populate_children(n, child_path); };
        }
        node.children.push_back(child);
    }
}

} // namespace

FileBrowser::FileBrowser(std::string root_path) : root_path_(std::move(root_path)) {
    tree_ = std::make_shared<QuitTreeView>();
    tree_->set_show_scrollbar(true);
    tree_->set_accent_color(kAccent);
    tree_->set_on_activate([this](tui::TreeNode& n) {
        bool is_file = n.children.empty() && !n.lazy_expand;
        if (is_file && on_open_) on_open_(n.id);
    });

    panel_ = std::make_shared<tui::Border>();
    panel_->set_border_style(tui::BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_title("Explorateur");
    panel_->set_child(tree_);

    rebuild();
}

void FileBrowser::set_on_quit(std::function<void()> cb) {
    static_cast<QuitTreeView*>(tree_.get())->set_on_quit(std::move(cb));
}

void FileBrowser::open_project(const std::string& path) {
    root_path_ = path;
    rebuild();
}

void FileBrowser::rebuild() {
    auto root = std::make_shared<tui::TreeNode>();
    root->label = fs::path(root_path_).filename().string();
    if (root->label.empty()) root->label = root_path_;
    root->id = root_path_;
    root->expanded = true;
    populate_children(*root, root_path_);
    tree_->set_root(root);
}

} // namespace ide
