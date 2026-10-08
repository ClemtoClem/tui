#include "SpreadsheetXml.hpp"

#include "../model/CellRef.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace sheetapp {

namespace {

std::string escape_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string unescape_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i);
            if (semi != std::string::npos) {
                std::string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") { out.push_back('&'); i = semi; continue; }
                if (ent == "lt") { out.push_back('<'); i = semi; continue; }
                if (ent == "gt") { out.push_back('>'); i = semi; continue; }
                if (ent == "quot") { out.push_back('"'); i = semi; continue; }
                if (ent == "apos") { out.push_back('\''); i = semi; continue; }
                if (!ent.empty() && ent[0] == '#') {
                    try {
                        long code = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                                        ? std::stol(ent.substr(2), nullptr, 16)
                                        : std::stol(ent.substr(1));
                        out.push_back(static_cast<char>(code));
                        i = semi;
                        continue;
                    } catch (...) {
                        // entite malformee : on la recopie telle quelle ci-dessous.
                    }
                }
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::optional<std::string> extract_attr(const std::string& tag, const std::string& name) {
    std::string pattern = name + "=\"";
    auto pos = tag.find(pattern);
    if (pos == std::string::npos) return std::nullopt;
    pos += pattern.size();
    auto end = tag.find('"', pos);
    if (end == std::string::npos) return std::nullopt;
    return tag.substr(pos, end - pos);
}

} // namespace

bool save_xml(const Sheet& sheet, const std::string& path, std::string& error) {
    std::ofstream f(path);
    if (!f) {
        error = "impossible d'ouvrir le fichier en ecriture";
        return false;
    }

    auto entries = sheet.entries();
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.first.row != b.first.row) return a.first.row < b.first.row;
        return a.first.col < b.first.col;
    });

    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << "<spreadsheet>\n";
    f << "  <sheet name=\"Feuille1\">\n";
    for (const auto& [pos, raw] : entries) {
        f << "    <cell ref=\"" << escape_text(cell_ref(pos)) << "\">" << escape_text(raw) << "</cell>\n";
    }
    f << "  </sheet>\n";
    f << "</spreadsheet>\n";

    if (!f) {
        error = "erreur d'ecriture";
        return false;
    }
    return true;
}

// Parseur minimal taille pour notre propre schema (pas de XML generique) :
// reconnait uniquement les balises <cell ref="..">texte</cell>, ignore le
// prologue <?xml..?>, les commentaires et les autres balises (<spreadsheet>,
// <sheet>) sans construire d'arbre DOM complet.
bool load_xml(Sheet& sheet, const std::string& path, std::string& error) {
    std::ifstream f(path);
    if (!f) {
        error = "impossible d'ouvrir le fichier";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string content = ss.str();

    Sheet loaded;
    size_t i = 0;
    while (i < content.size()) {
        size_t lt = content.find('<', i);
        if (lt == std::string::npos) break;

        if (content.compare(lt, 4, "<!--") == 0) {
            size_t end = content.find("-->", lt);
            if (end == std::string::npos) break;
            i = end + 3;
            continue;
        }
        if (content.compare(lt, 2, "<?") == 0) {
            size_t end = content.find("?>", lt);
            if (end == std::string::npos) break;
            i = end + 2;
            continue;
        }

        size_t gt = content.find('>', lt);
        if (gt == std::string::npos) {
            error = "XML malforme (balise non fermee)";
            return false;
        }
        std::string tag = content.substr(lt + 1, gt - lt - 1);
        bool self_closing = !tag.empty() && tag.back() == '/';
        if (self_closing) tag.pop_back();

        size_t name_end = tag.find_first_of(" \t\r\n");
        std::string tag_name = name_end == std::string::npos ? tag : tag.substr(0, name_end);
        bool closing = !tag_name.empty() && tag_name[0] == '/';
        if (closing) tag_name = tag_name.substr(1);

        if (!closing && tag_name == "cell") {
            auto ref_attr = extract_attr(tag, "ref");
            std::string text;
            if (!self_closing) {
                size_t content_start = gt + 1;
                size_t close_pos = content.find("</cell>", content_start);
                if (close_pos == std::string::npos) {
                    error = "balise <cell> non fermee";
                    return false;
                }
                text = unescape_text(content.substr(content_start, close_pos - content_start));
                i = close_pos + 7; // longueur de "</cell>"
            } else {
                i = gt + 1;
            }
            if (ref_attr) {
                if (auto pos = parse_cell_ref(*ref_attr)) loaded.set_cell(*pos, text);
            }
            continue;
        }
        i = gt + 1;
    }

    sheet = std::move(loaded);
    return true;
}

} // namespace sheetapp
