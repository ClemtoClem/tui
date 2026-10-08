#!/usr/bin/env bash
# Point d'entrée unique demandé : "make run --tests" / "make run --example
# NOM" ne peuvent pas exister tels quels (make intercepte tout argument
# commençant par "-" comme sa propre option AVANT de lire le Makefile, et
# échoue sur --tests/--example qu'il ne connaît pas). Ce script traduit
# cette syntaxe en cibles make réelles (run-tests, run-<nom>, list-applications).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

usage() {
    echo "Usage: ./run [--tests | --example [NOM] | --application [NOM]]" >&2
    exit 1
}

case "${1:-}" in
    --tests)
        exec make run-tests
        ;;
    --application)
        if [ -n "${2:-}" ]; then
            exec make "run-$2"
        else
            exec make list-applications
        fi
        ;;
    "")
        exec make run
        ;;
    *)
        usage
        ;;
esac
