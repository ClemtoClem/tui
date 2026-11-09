/* apps/yocto-tui/project/Presets.hpp */
#pragma once

#include "ProjectSpec.hpp"

namespace yocto {

/// Projet du TP « formation-image » (équivalent de tp-yocto.sh setup +
/// layers + config + patch-bc) : layer meta-formation, meta-oe, image
/// avec SSH et utilisateur stagiaire, scripts, applications C, Makefile
/// et CMake, motd, interfaces réseau, applet ntpd de BusyBox, patch de bc.
[[nodiscard]] ProjectSpec formation_preset(const std::string& stagiaire_password = "formation");

/// Modèles utilisés par le bouton « Ajouter » de chaque éditeur.
[[nodiscard]] CustomApp      app_template(AppKind kind, const std::string& name);
[[nodiscard]] RootfsFile     rootfs_template();
[[nodiscard]] ConfigFragment fragment_template();
[[nodiscard]] PatchSpec      patch_template();
[[nodiscard]] ExtraLayer     extra_layer_template();

} // namespace yocto
