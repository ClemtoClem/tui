/* @file project/Presets.cpp */

#include "project/Presets.hpp"

namespace yocto {

namespace {

const char* kBonjour =
    "#!/bin/sh\n"
    "# Script de formation : affiche un message et quelques informations systeme\n"
    "echo \"Bonjour depuis $(hostname) !\"\n"
    "echo \"Noyau  : $(uname -r)\"\n"
    "echo \"Date   : $(date)\"\n";

const char* kInfoPy =
    "#!/usr/bin/env python3\n"
    "# Script Python de formation : informations systeme\n"
    "import os\n"
    "import sys\n"
    "\n"
    "print(\"Python  :\", sys.version.split()[0])\n"
    "print(\"Systeme :\", os.uname().sysname, os.uname().release)\n"
    "print(\"Machine :\", os.uname().machine)\n"
    "print(\"PID     :\", os.getpid())\n";

const char* kHelloC =
    "#include <stdio.h>\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"Hello depuis la toolchain Yocto !\\n\");\n"
    "    return 0;\n"
    "}\n";

const char* kMainC =
    "#include <stdio.h>\n"
    "#include <string.h>\n"
    "#include <unistd.h>\n"
    "#include <sys/sysinfo.h>\n"
    "\n"
    "int main(int argc, char *argv[])\n"
    "{\n"
    "    char host[64];\n"
    "    struct sysinfo si;\n"
    "\n"
    "    (void)argc;\n"
    "\n"
    "    if (gethostname(host, sizeof(host)) != 0)\n"
    "        strcpy(host, \"inconnu\");\n"
    "    host[sizeof(host) - 1] = '\\0';\n"
    "\n"
    "    if (sysinfo(&si) != 0) {\n"
    "        perror(\"sysinfo\");\n"
    "        return 1;\n"
    "    }\n"
    "\n"
    "    printf(\"Programme : %s\\n\", argv[0]);\n"
    "    printf(\"Hote      : %s\\n\", host);\n"
    "    printf(\"Uptime    : %ld s\\n\", (long)si.uptime);\n"
    "    printf(\"RAM libre : %lu Mo\\n\",\n"
    "           ((unsigned long)si.freeram * si.mem_unit) >> 20);\n"
    "    return 0;\n"
    "}\n";

const char* kAppliMakefile =
    "TARGET  = appli-make\n"
    "CC     ?= gcc\n"
    "CFLAGS += -Wall\n"
    "bindir ?= /usr/bin\n"
    "DESTDIR ?=\n"
    "\n"
    "all: $(TARGET)\n"
    "\n"
    "$(TARGET): main.o\n"
    "\t$(CC) $(LDFLAGS) -o $@ $^\n"
    "\n"
    "main.o: main.c\n"
    "\t$(CC) $(CFLAGS) -c -o $@ $<\n"
    "\n"
    "install: all\n"
    "\tinstall -d $(DESTDIR)$(bindir)\n"
    "\tinstall -m 0755 $(TARGET) $(DESTDIR)$(bindir)/$(TARGET)\n"
    "\n"
    "clean:\n"
    "\trm -f $(TARGET) main.o\n"
    "\n"
    ".PHONY: all install clean\n";

const char* kAppliCMake =
    "cmake_minimum_required(VERSION 3.10)\n"
    "project(appli-cmake C)\n"
    "\n"
    "include(GNUInstallDirs)\n"
    "\n"
    "add_executable(appli-cmake main.c)\n"
    "target_compile_options(appli-cmake PRIVATE -Wall)\n"
    "\n"
    "install(TARGETS appli-cmake RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})\n";

// Les espaces en début de ligne et la ligne vide de tête sont voulus.
const char* kMotd =
    "\n"
    " ==========================================\n"
    "   Image de formation Yocto - Scarthgap\n"
    "   Bienvenue ! Cette cible est buildee\n"
    "   avec votre propre layer meta-formation.\n"
    " ==========================================\n"
    "\n";

const char* kNtpdInit =
    "#!/bin/sh\n"
    "# Demarrage / arret du client NTP BusyBox (formation)\n"
    "\n"
    "case \"$1\" in\n"
    "    start)\n"
    "        echo \"Demarrage de ntpd\"\n"
    "        /usr/sbin/ntpd -p pool.ntp.org\n"
    "        ;;\n"
    "    stop)\n"
    "        echo \"Arret de ntpd\"\n"
    "        killall ntpd\n"
    "        ;;\n"
    "    restart)\n"
    "        \"$0\" stop\n"
    "        \"$0\" start\n"
    "        ;;\n"
    "    *)\n"
    "        echo \"Usage: $0 {start|stop|restart}\" >&2\n"
    "        exit 1\n"
    "        ;;\n"
    "esac\n"
    "exit 0\n";

const char* kInterfaces =
    "# /etc/network/interfaces : configuration statique (formation)\n"
    "auto lo\n"
    "iface lo inet loopback\n"
    "\n"
    "auto eth0\n"
    "iface eth0 inet static\n"
    "    address 10.0.2.15\n"
    "    netmask 255.255.255.0\n"
    "    gateway 10.0.2.2\n";

} // namespace

ProjectSpec formation_preset(const std::string& stagiaire_password) {
    ProjectSpec p;
    p.layer_name = "meta-formation";
    p.layer_priority = 6;
    p.layer_depends = "core openembedded-layer";

    p.image.name = "formation-image";
    p.image.base = "core-image-minimal";
    p.image.summary = "Image de formation Yocto";
    p.image.hostname = "formation-qemu";
    p.image.extra_packages = "strace lsof bc htop jq";
    p.image.features = "ssh-server-dropbear";
    p.image.empty_root_password = true;
    p.image.users.push_back({"stagiaire", stagiaire_password, ""});

    p.extra_layers.push_back({"meta-openembedded", "https://git.openembedded.org/meta-openembedded", "", "meta-oe", true});

    {
        CustomApp a;
        a.name = "formation-scripts";
        a.summary = "Scripts de démonstration pour l'image de formation";
        a.kind = AppKind::Scripts;
        a.files = {
            {"bonjour.sh", "/usr/bin/bonjour", "0755", kBonjour},
            {"info.py", "/usr/bin/info-systeme", "0755", kInfoPy},
        };
        p.apps.push_back(a);
    }
    {
        CustomApp a;
        a.name = "formation-apps";
        a.summary = "Applications C de démonstration (compilées par Yocto)";
        a.kind = AppKind::CFiles;
        a.files = {
            {"hello.c", "/usr/bin/hello-yocto", "0755", kHelloC},
            {"main.c", "/usr/bin/appli-make-yocto", "0755", kMainC},
        };
        p.apps.push_back(a);
    }
    {
        CustomApp a;
        a.name = "ntpd-formation";
        a.summary = "Script d'init BusyBox pour ntpd (formation)";
        a.kind = AppKind::Scripts;
        a.rdepends = "busybox";
        a.files = {
            {"ntpd-formation", "/etc/init.d/ntpd-formation", "0755", kNtpdInit},
            {"ntpd.cfg", "/etc/default/ntpd", "0644", "CONFIG_NTPD=y\n"},
        };
        p.apps.push_back(a);
    }
    {
        // Mêmes sources que la démo SDK : construites ici par Yocto.
        CustomApp a;
        a.name = "appli-make";
        a.summary = "Application C construite par son Makefile";
        a.kind = AppKind::Make;
        a.in_image = false;
        a.files = {
            {"Makefile", "", "0644", kAppliMakefile},
            {"main.c", "", "0644", kMainC},
        };
        p.apps.push_back(a);
    }
    {
        CustomApp a;
        a.name = "appli-cmake";
        a.summary = "Application C construite par CMake";
        a.kind = AppKind::CMake;
        a.in_image = false;
        a.files = {
            {"CMakeLists.txt", "", "0644", kAppliCMake},
            {"main.c", "", "0644", kMainC},
        };
        p.apps.push_back(a);
    }

    p.rootfs_files.push_back({"/etc/motd", "0644", "base-files", kMotd, true});
    p.rootfs_files.push_back({"/etc/network/interfaces", "0644", "init-ifupdown", kInterfaces, true});

    p.fragments.push_back({"busybox", "ntpd.cfg", "CONFIG_NTPD=y\n", true});

    {
        PatchSpec pa;
        pa.recipe = "bc";
        pa.name = "bc-edition-formation";
        pa.method = PatchMethod::Devtool;
        pa.search = "This is free software with ABSOLUTELY NO WARRANTY";
        pa.replace = "This is free software with ABSOLUTELY NO WARRANTY - Édition formation";
        pa.message = "Add formation edition mention";
        pa.upstream_status = "Inappropriate [formation interne, TP Yocto]";
        p.patches.push_back(pa);
    }
    return p;
}

CustomApp app_template(AppKind kind, const std::string& name) {
    CustomApp a;
    a.name = name;
    a.kind = kind;
    a.summary = "Application " + name;
    switch (kind) {
        case AppKind::Scripts:
            a.files = {{"hello.sh", "/usr/bin/" + name, "0755", "#!/bin/sh\necho \"Bonjour depuis " + name + "\"\n"}};
            break;
        case AppKind::CFiles:
            a.files = {{"main.c", "/usr/bin/" + name, "0755", kHelloC}};
            break;
        case AppKind::Make:
            a.files = {{"Makefile", "", "0644", kAppliMakefile}, {"main.c", "", "0644", kMainC}};
            break;
        case AppKind::CMake:
            a.files = {{"CMakeLists.txt", "", "0644", kAppliCMake}, {"main.c", "", "0644", kMainC}};
            break;
    }
    return a;
}

RootfsFile rootfs_template() {
    return RootfsFile{"/etc/exemple.conf", "0644", "", "# Fichier de configuration de la cible\n", true};
}

ConfigFragment fragment_template() {
    return ConfigFragment{"linux-yocto", "fragment.cfg", "# CONFIG_EXEMPLE is not set\n", true};
}

PatchSpec patch_template() {
    PatchSpec p;
    p.recipe = "recette";
    p.name = "mon-patch";
    p.message = "Description de la modification";
    return p;
}

ExtraLayer extra_layer_template() {
    return ExtraLayer{"meta-openembedded", "https://git.openembedded.org/meta-openembedded", "", "meta-oe", true};
}

} // namespace yocto
