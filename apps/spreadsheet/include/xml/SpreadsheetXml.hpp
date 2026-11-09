#pragma once

// Lecture/ecriture d'un schema XML maison pour le tableur (pas de
// compatibilite .xlsx/OOXML, ni de parseur XML generique) :
//
// <?xml version="1.0" encoding="UTF-8"?>
// <spreadsheet>
//   <sheet name="Feuille1">
//     <cell ref="A1">Bonjour</cell>
//     <cell ref="B2">=SUM(A1:A5)</cell>
//   </sheet>
// </spreadsheet>

#include "model/Sheet.hpp"

#include <string>

namespace sheetapp {

bool save_xml(const Sheet& sheet, const std::string& path, std::string& error);
bool load_xml(Sheet& sheet, const std::string& path, std::string& error);

} // namespace sheetapp
