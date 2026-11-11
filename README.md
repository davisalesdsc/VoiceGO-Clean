# VoiceGO Clean

Plugin de áudio (VST3 / AU / app) que separa uma gravação de voz em três partes,
cada uma com seu próprio controle:

| Controle | O que é |
|---|---|
| **AMBIENCE** | ruído de fundo constante: chiado, zumbido, ar-condicionado, som da sala |
| **VOICE** | a voz direta (seca) |
| **VOICE REVERB** | a reverberação da sala sobre a voz |

Cada knob vai de **OFF** (remove) a **+12 dB**, com **0 dB** = original. Também há **S** (solo) e **M** (mute)
por componente, **Input/Output** com medidores, **bypass** (power) e duas visualizações:
**WAVE** (entrada x saída) e **SPLIT** (espectro de cada componente).

![WAVE](docs/voicego-clean-wave.png)
![SPLIT](docs/voicego-clean-split.png)

## Instalar (macOS)

Baixe `VoiceGO-Clean-<versão>-macOS-Universal.pkg` (em *Releases* ou em *Actions › Artifacts*) e dê dois cliques.
Um instalador só para **Apple Silicon e Intel**. Ele coloca:

- `/Library/Audio/Plug-Ins/VST3/VoiceGO Clean.vst3`
- `/Library/Audio/Plug-Ins/Components/VoiceGO Clean.component` (AU)
- `/Applications/VoiceGO Clean.app`
- desinstalador em `/Library/Application Support/VoiceGO Clean`

Primeira vez (instalador sem assinatura da Apple): se aparecer "desenvolvedor não identificado",
vá em *Ajustes do Sistema › Privacidade e Segurança › Abrir Mesmo Assim*.

## Como funciona

**Motor neural (padrão, selo NEURAL na interface):**

- A voz é identificada pela rede **DeepFilterNet3** (embutida no plugin, roda localmente, sem internet).
  A rede trabalha a 48 kHz; em 44,1/88,2/96 kHz o plugin reamostra com atraso exato e compensa a resposta do filtro.
- A estimativa da rede vira uma **máscara por frequência** aplicada ao sinal original — as fases e o "ar" da voz
  vêm da gravação, não de uma ressíntese.
- **Ambience** = tudo o que a rede não reconhece como voz (ruído, ar-condicionado, trânsito, música de fundo…).
- **Voice reverb** = cauda da sala estimada só sobre a voz já limpa (modelo de decaimento, RT60 0,6 s),
  então ruído não vaza para o knob Voice.
- As três partes somam sempre o original: com tudo em 0 dB a saída é idêntica à entrada.
- Latência: ~61 ms a 48 kHz (rede + análise), informada à DAW (compensação automática).

**Motor DSP (reserva, selo DSP):** se a rede não puder ser carregada, o plugin usa *minimum statistics* +
máscara de Wiener *decision-directed* (latência 2048 amostras).

Créditos e licenças de terceiros: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Compilar

O workflow `.github/workflows/build.yml` faz tudo a cada push: compila num Mac da nuvem em **Universal
(arm64 + x86_64)**, roda os testes, monta o `.pkg`, instala de verdade e valida o AU com o `auval` da Apple.
A cada push na `main` com tudo passando, a Release `v<versão>` (versão do `CMakeLists.txt`) é criada ou atualizada com o `.pkg`. Versão nova = mudar `project(VoiceGOClean VERSION ...)`.

Assinatura/notarização (opcional): configure os secrets `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD`,
`APPLE_ID`, `APPLE_TEAM_ID` e `APPLE_APP_PASSWORD`. Sem eles, os bundles saem com assinatura ad-hoc.

Compilar exige **Rust** (`cargo`) além do CMake: a rede vem de `dfn/` (wrapper do libDF).
Opções: `-DVOICEGO_DFN=neural` (padrão), `stub` (substituto só para testes) ou `none` (só DSP).

Testes (qualquer sistema):

```bash
cmake -S . -B build -DVOICEGO_BUILD_TESTS=ON && cmake --build build --target VoiceGOCleanTest
./build/VoiceGOCleanTest_artefacts/Release/VoiceGOCleanTest selftest          # motor DSP
./build/VoiceGOCleanTest_artefacts/Release/VoiceGOCleanTest speech limpa.wav ruido.wav sala.wav saida/   # voz real
```

O CI também publica *Exemplos-de-audio* (entrada, só voz, só ambiente, só reverb) a cada build.

## Formatos

VST3 e AU (macOS), VST3 (Windows/Linux) e app standalone. **VST2 não é possível**: a Steinberg deixou de
licenciar o SDK do VST2 em 2018 e ele não pode ser distribuído. Hoje todas as DAWs leem VST3.
