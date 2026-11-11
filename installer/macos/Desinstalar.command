#!/bin/bash
# Remove o VoiceGO Clean do Mac (plugins VST3/AU e app).
fim() { echo; read -n 1 -s -r -p "Pressione qualquer tecla para fechar..."; echo; exit "${1:-0}"; }
clear
echo "=============================================="
echo "  Desinstalar VoiceGO Clean"
echo "=============================================="
echo
echo "Isto remove os plugins VST3 e AU e o app VoiceGO Clean."
read -r -p "Continuar? [s/N] " r
case "$r" in s|S) ;; *) echo "Cancelado."; fim 0 ;; esac

echo
echo "O macOS vai pedir a sua senha de administrador."
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/VoiceGO Clean.vst3" \
            "/Library/Audio/Plug-Ins/Components/VoiceGO Clean.component" \
            "/Applications/VoiceGO Clean.app" \
            "/Library/Application Support/VoiceGO Clean" || fim 1
for id in $(pkgutil --pkgs | grep '^com\.voicego\.clean'); do
  sudo pkgutil --forget "$id" >/dev/null
done
killall -9 AudioComponentRegistrar 2>/dev/null
echo
echo "Removido."
fim 0
