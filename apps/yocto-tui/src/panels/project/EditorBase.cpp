/* @file panels/project/EditorBase.cpp */

#include "panels/project/Editors.hpp"

namespace yocto {

void EditorBase::bind(const std::shared_ptr<tui::Input>& in, std::function<void(const std::string&)> set) {
    in->set_on_change([this, set = std::move(set)](const std::string& v) {
        if (loading_) return;
        set(v);
        changed();
    });
}

void EditorBase::bind(const std::shared_ptr<tui::Checkbox>& cb, std::function<void(bool)> set) {
    cb->set_on_change([this, set = std::move(set)](bool v) {
        if (loading_) return;
        set(v);
        changed();
    });
}

void EditorBase::bind(const std::shared_ptr<tui::TextArea>& ta, std::function<void(const std::string&)> set) {
    // Capture faible : le TextArea possède le callback, une capture
    // forte créerait un cycle de shared_ptr.
    std::weak_ptr<tui::TextArea> weak = ta;
    ta->set_on_change([this, weak, set = std::move(set)] {
        if (loading_) return;
        if (auto t = weak.lock()) {
            set(t->text());
            changed();
        }
    });
}

void EditorBase::bind(const std::shared_ptr<tui::RadioSet>& rs, std::function<void(size_t)> set) {
    rs->set_on_change([this, set = std::move(set)](size_t i) {
        if (loading_) return;
        set(i);
        changed();
    });
}

} // namespace yocto
