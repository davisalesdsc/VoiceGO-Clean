#!/bin/bash
# Monta o instalador .pkg do VoiceGO Clean (um arquivo só, Universal:
# Apple Silicon + Intel), com escolha de componentes (VST3 / AU / App).
#
#   build-pkg.sh <pasta_com_os_bundles> <versao> <saida.pkg>
#
# <pasta_com_os_bundles> precisa ter (Universal, já assinados):
#     "VoiceGO Clean.vst3"   "VoiceGO Clean.component"   "VoiceGO Clean.app"
#
# Assinatura opcional: com MAC_INSTALLER_IDENTITY="Developer ID Installer: ..."
# no ambiente, o .pkg sai assinado (necessário para notarizar).
set -euo pipefail

BUNDLES="$(cd "$1" && pwd)"
VERSION="$2"
OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"

NAME="VoiceGO Clean"
ID="com.voicego.clean"
SUPPORT="/Library/Application Support/VoiceGO Clean"

# confere que todos os binários são Universal
for b in "$BUNDLES/$NAME.vst3" "$BUNDLES/$NAME.component" "$BUNDLES/$NAME.app"; do
  bin="$b/Contents/MacOS/$NAME"
  [ -f "$bin" ] || { echo "ERRO: faltando $bin"; exit 1; }
  if command -v lipo >/dev/null; then
    lipo "$bin" -verify_arch arm64 x86_64 || { echo "ERRO: $bin não é Universal"; exit 1; }
  fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs"

# --- componente de bundle (plugin/app): sem "relocação" ----------------------
# Por padrão o Installer procura o bundle onde o usuário o tiver movido e
# atualiza lá; para plugin isso espalha cópias. Desligamos.
bundle_pkg () {   # <arquivo_bundle> <destino> <id> <scripts|-> <saida>
  local bundle="$1" dest="$2" id="$3" scripts="$4" out="$5"
  local root="$WORK/root-$id"
  mkdir -p "$root"
  ditto "$BUNDLES/$bundle" "$root/$bundle"
  cat > "$WORK/$id.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<array>
  <dict>
    <key>RootRelativeBundlePath</key><string>$bundle</string>
    <key>BundleIsRelocatable</key><false/>
    <key>BundleIsVersionChecked</key><false/>
    <key>BundleHasStrictIdentifier</key><false/>
    <key>BundleOverwriteAction</key><string>upgrade</string>
  </dict>
</array>
</plist>
PLIST
  plutil -lint "$WORK/$id.plist" >/dev/null
  local extra=()
  [ "$scripts" != "-" ] && extra=(--scripts "$scripts")
  pkgbuild --root "$root" --component-plist "$WORK/$id.plist" \
           --identifier "$id" --version "$VERSION" --install-location "$dest" \
           ${extra[@]+"${extra[@]}"} "$WORK/pkgs/$out" >/dev/null
  echo "  componente: $out ($bundle -> $dest)"
}

# scripts: apagam a versão anterior antes de copiar e, no AU, avisam o macOS
mkscripts () {   # <nome> <caminho_a_apagar> [au]
  local d="$WORK/scripts-$1"
  mkdir -p "$d"
  cat > "$d/preinstall" <<EOF
#!/bin/bash
rm -rf "$2"
exit 0
EOF
  if [ "${3:-}" = "au" ]; then
    cat > "$d/postinstall" <<'EOF'
#!/bin/bash
# o Logic/GarageBand/MainStage só enxergam AU novo depois disto
killall -9 AudioComponentRegistrar 2>/dev/null
exit 0
EOF
  fi
  chmod +x "$d"/*
  echo "$d"
}

echo "Montando o instalador $NAME $VERSION (Universal)"
bundle_pkg "$NAME.vst3"      "/Library/Audio/Plug-Ins/VST3"       "$ID.vst3" \
           "$(mkscripts vst3 "/Library/Audio/Plug-Ins/VST3/$NAME.vst3")" vst3.pkg
bundle_pkg "$NAME.component" "/Library/Audio/Plug-Ins/Components" "$ID.au" \
           "$(mkscripts au "/Library/Audio/Plug-Ins/Components/$NAME.component" au)" au.pkg
bundle_pkg "$NAME.app"       "/Applications"                      "$ID.app" \
           "$(mkscripts app "/Applications/$NAME.app")" app.pkg

# --- suporte: desinstalador + LEIA-ME (sempre instalado, invisível) ----------
mkdir -p "$WORK/root-suporte"
cp "$HERE/Desinstalar.command" "$WORK/root-suporte/Desinstalar VoiceGO Clean.command"
chmod +x "$WORK/root-suporte/Desinstalar VoiceGO Clean.command"
cp "$HERE/../../README.md" "$WORK/root-suporte/LEIA-ME.md"
cp "$HERE/../../THIRD-PARTY-NOTICES.md" "$WORK/root-suporte/LICENCAS-DE-TERCEIROS.md"
pkgbuild --root "$WORK/root-suporte" --identifier "$ID.suporte" --version "$VERSION" \
         --install-location "$SUPPORT" "$WORK/pkgs/suporte.pkg" >/dev/null
echo "  componente: suporte.pkg (desinstalador -> $SUPPORT)"

# --- Distribution: o que a tela do instalador mostra ---------------------------
cat > "$WORK/Distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$NAME $VERSION</title>
  <organization>com.voicego</organization>
  <welcome file="welcome.html" mime-type="text/html"/>
  <conclusion file="conclusion.html" mime-type="text/html"/>
  <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
  <domains enable_localSystem="true" enable_anywhere="false" enable_currentUserHome="false"/>
  <volume-check>
    <allowed-os-versions><os-version min="10.13"/></allowed-os-versions>
  </volume-check>
  <choices-outline>
    <line choice="vst3"/>
    <line choice="au"/>
    <line choice="app"/>
    <line choice="suporte"/>
  </choices-outline>
  <choice id="vst3" title="Plugin VST3" description="Para REAPER, Ableton Live, Cubase, Studio One, Bitwig, FL Studio e outros.">
    <pkg-ref id="$ID.vst3"/>
  </choice>
  <choice id="au" title="Plugin AU (Audio Unit)" description="Para Logic Pro, MainStage e GarageBand (e também REAPER/Ableton).">
    <pkg-ref id="$ID.au"/>
  </choice>
  <choice id="app" title="App VoiceGO Clean" description="Usa o microfone/interface direto, sem precisar de DAW.">
    <pkg-ref id="$ID.app"/>
  </choice>
  <choice id="suporte" title="Desinstalador" visible="false">
    <pkg-ref id="$ID.suporte"/>
  </choice>
  <pkg-ref id="$ID.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
  <pkg-ref id="$ID.au" version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
  <pkg-ref id="$ID.app" version="$VERSION" onConclusion="none">app.pkg</pkg-ref>
  <pkg-ref id="$ID.suporte" version="$VERSION" onConclusion="none">suporte.pkg</pkg-ref>
</installer-gui-script>
EOF
xmllint --noout "$WORK/Distribution.xml"

# --- telas (boas-vindas / conclusão) com a versão ------------------------------
mkdir -p "$WORK/resources"
for f in welcome.html conclusion.html; do
  sed -e "s/@VERSAO@/$VERSION/g" "$HERE/resources/$f" > "$WORK/resources/$f"
done

SIGN=()
if [ -n "${MAC_INSTALLER_IDENTITY:-}" ]; then
  SIGN=(--sign "$MAC_INSTALLER_IDENTITY")
  echo "  assinando com: $MAC_INSTALLER_IDENTITY"
fi
productbuild --distribution "$WORK/Distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" ${SIGN[@]+"${SIGN[@]}"} "$OUT"
echo "Pronto: $OUT ($(( $(stat -f%z "$OUT") / 1048576 )) MB)"
