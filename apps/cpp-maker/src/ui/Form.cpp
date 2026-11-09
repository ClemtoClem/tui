/* @file ui/Form.cpp */

#include "ui/Form.hpp"

#include "Theme.hpp"
#include "core/Util.hpp"

#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widgets/Label.hpp>

namespace ui {

using schema::FieldType;

std::string field_text(const schema::Field& f, const toml::Value* v) {
    if (!v) return {};
    if (f.type == FieldType::Map && v->is_table()) {
        std::string r;
        for (size_t i = 0; i < v->size(); ++i) {
            if (i) r += ", ";
            r += v->keys()[i] + "=" + (v->value_at(i).is_string() ? v->value_at(i).as_string() : toml::format_value(v->value_at(i)));
        }
        return r;
    }
    if (f.type == FieldType::Boolean && v->is_bool()) return v->as_bool() ? "true" : "false";
    return toml::value_to_text(*v);
}

std::optional<toml::Value> field_value(const schema::Field& f, const std::string& raw) {
    std::string text = util::trim(raw);
    if (text.empty()) return std::nullopt;
    if (f.type == FieldType::Map && !text.starts_with("{")) {
        toml::Value t = toml::Value::table();
        t.set_inline(true);
        for (auto& item : util::split_list(text)) {
            auto eq = item.find('=');
            if (eq == std::string::npos) util::fail("{}.{} : « CLE=valeur » attendu, reçu '{}'", f.section, f.key, item);
            t.set(util::trim(item.substr(0, eq)), toml::Value(util::trim(item.substr(eq + 1))));
        }
        return t;
    }
    if (f.type == FieldType::String) return toml::Value(raw);   // espaces significatifs conservés
    return schema::parse_input(f, text);
}

std::string Form::Field::text() const {
    if (input) return input->text();
    if (choice) return choice->value();
    return {};
}

Form::Form(schema::FileKind kind, std::vector<FormSection> sections, std::set<std::string> hidden)
    : kind_(kind), root_(std::make_shared<tui::Vertical>()) {
    constexpr int kLabelWidth = 26;
    bool first = true;
    for (auto& sec : sections) {
        auto specs = schema::fields(kind, sec.pattern);
        if (specs.empty()) continue;
        std::string title = sec.title;
        if (title.empty())
            if (auto* s = schema::find_section(kind, sec.table)) title = s->title;
        if (!first) root_->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::fixed(1));
        first = false;
        auto head = std::make_shared<tui::Label>(std::format("▌ {}  [{}]", title, toml::join_path(sec.table)));
        head->add_class("section");
        root_->add_child(head, tui::LayoutParams::fixed(1));
        for (auto* spec : specs) {
            std::string dotted = sec.pattern + "." + spec->key;
            if (hidden.count(dotted) || hidden.count(spec->key)) continue;
            Field fld;
            fld.spec = spec;
            fld.table = sec.table;
            auto row = std::make_shared<tui::Horizontal>();
            auto label = std::make_shared<tui::Label>("  " + spec->label);
            label->add_class("field-label");
            row->add_child(label, tui::LayoutParams::fixed(kLabelWidth));
            size_t index = fields_.size();
            auto focus_cb = [this, index] { help_for(fields_[index]); };
            if (spec->type == FieldType::Enum || spec->type == FieldType::Boolean) {
                auto c = std::make_shared<ChoiceField>();
                std::vector<std::string> values, labels;
                if (spec->type == FieldType::Boolean) values = {"", "true", "false"};
                else {
                    values = spec->choices;
                    if (values.empty() || !values.front().empty()) values.insert(values.begin(), "");
                }
                for (auto& v : values)
                    labels.push_back(v.empty() ? (spec->placeholder.empty() ? "(défaut)" : "(défaut : " + spec->placeholder + ")") : v);
                c->set_choices(values, labels);
                c->set_on_focus(focus_cb);
                c->set_on_change([this](const std::string&) { if (on_change_) on_change_(); });
                fld.choice = c;
                row->add_child(c, tui::LayoutParams::auto_size());
                row->add_child(std::make_shared<tui::Label>(""), tui::LayoutParams::stretch(1));
            } else {
                auto in = std::make_shared<FormInput>();
                std::string ph = spec->placeholder;
                if (spec->type == FieldType::Map && ph.empty()) ph = "CLE=valeur, CLE2=valeur";
                in->set_placeholder(ph);
                if (spec->type == FieldType::Integer)
                    in->set_validator([](char32_t ch) { return (ch >= U'0' && ch <= U'9') || ch == U'-'; });
                in->set_on_focus(focus_cb);
                in->set_on_change([this](const std::string&) { if (on_change_) on_change_(); });
                fld.input = in;
                row->add_child(in, tui::LayoutParams::stretch(1));
            }
            root_->add_child(row, tui::LayoutParams::fixed(1));
            fields_.push_back(std::move(fld));
        }
    }
    if (fields_.empty()) root_->add_child(std::make_shared<tui::Label>("  (aucun champ)"), tui::LayoutParams::fixed(1));
}

std::shared_ptr<tui::Widget> Form::first_field() const {
    for (auto& f : fields_) {
        if (f.input) return f.input;
        if (f.choice) return f.choice;
    }
    return nullptr;
}

void Form::set_accent(tui::Color c) {
    for (auto& f : fields_)
        if (f.choice) f.choice->set_accent(c);
}

void Form::load(const toml::Document& doc) {
    for (auto& f : fields_) {
        f.original = field_text(*f.spec, doc.get(f.table, f.spec->key));
        if (f.input) f.input->set_text(f.original);
        if (f.choice) f.choice->set_value(f.original);
    }
}

bool Form::dirty() const {
    for (auto& f : fields_)
        if (f.text() != f.original) return true;
    return false;
}

void Form::apply(toml::Document& doc) const {
    for (auto& f : fields_) {
        std::string cur = f.text();
        if (cur == f.original) continue;
        auto v = field_value(*f.spec, cur);
        if (!v) doc.remove(f.table, f.spec->key);
        else doc.set(f.table, f.spec->key, *v);
    }
}

void Form::help_for(const Field& f) const {
    if (!on_help_) return;
    std::string h = std::format("{}.{} — {}", toml::join_path(f.table), f.spec->key, f.spec->help.empty() ? f.spec->label : f.spec->help);
    if (f.spec->type == FieldType::StringList) h += "  (liste : valeurs séparées par des virgules)";
    if (f.spec->type == FieldType::Enum || f.spec->type == FieldType::Boolean) h += "  (←/→ pour changer)";
    if (!f.spec->placeholder.empty()) h += "  [défaut : " + f.spec->placeholder + "]";
    on_help_(h);
}

} // namespace ui
