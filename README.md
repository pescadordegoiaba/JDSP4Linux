<h1 align="center">
  <img alt="Icon" width="75" src="https://github.com/Audio4Linux/JDSP4Linux/blob/master/resources/icons/icon.png?raw=true">
  <br>
  JamesDSP for Linux
  <br>
</h1>
<h4 align="center">Open-source sound effects for PipeWire and PulseAudio</h4>
<p align="center">
  <a href="https://github.com/Audio4Linux/JDSP4Linux/releases">
  	<img alt="GitHub release (latest by date)" src="https://img.shields.io/github/v/release/Audio4Linux/JDSP4Linux">
  </a>
  <a href="https://github.com/Audio4Linux/JDSP4Linux/blob/master/LICENSE">
      <img alt="License" src="https://img.shields.io/github/license/Audio4Linux/JDSP4Linux">
  </a>
  <a href="https://github.com/Audio4Linux/JDSP4Linux/">
    <img alt="Windows build" src="https://img.shields.io/github/repo-size/Audio4Linux/JDSP4Linux">
  </a>
</p>
<p align="center">
  <a href="#atualização-de-outubro-de-2026">Atualização de 2026</a> •
  <a href="#features">Features</a> •
  <a href="#installation">Installation</a> •
  <a href="#scripting--ipc-apis">Scripting/API</a> •
  <a href="#contributors">Contributors</a> •
  <a href="#license">License</a> 
</p>

<p align="center">
   <img alt="Screenshot" width="702" src="https://github.com/Audio4Linux/JDSP4Linux/blob/master/meta/screenshot.png?raw=true">
</p>

<p align="center">
Linux port developed by <a href="https://github.com/thepbone">Tim Schneeberger (@thepbone)</a>
<p/><p align="center">
<a href="https://github.com/james34602/JamesDSPManager">JamesDSP</a> was initially published as an audio effects processor<br>for Android devices and is written by <a href="https://github.com/james34602">James Fung (@james34602)</a>.
</p>
<p align="center">
    Feel free to join our <a href="https://t.me/joinchat/FTKC2A2bolHkFAyO-fuPjw">Telegram group</a> for support and updates
</p>
<p align="center">
    The last tagged upstream release is <strong>2.7.0</strong> (10 December 2023). This fork's October 2026 update is documented below. The upstream project remains <a href="https://github.com/Audio4Linux/JDSP4Linux">Audio4Linux/JDSP4Linux</a>.
</p>

____________

## Atualização de outubro de 2026

A última versão publicada do JamesDSP para Linux é a **2.7.0**, de 10 de dezembro de 2023. Até outubro de 2026 se passaram quase três anos sem uma versão nova. O repositório original, [Audio4Linux/JDSP4Linux](https://github.com/Audio4Linux/JDSP4Linux), não ficou vazio nesse intervalo: o Crowdin continuou sincronizando traduções, e entraram correções pequenas de compilação (Qt 6.9 em abril de 2025, PipeWire mais novo em março de 2025, GCC mais novo no fim de 2024), um ícone monocromático de bandeja (outubro de 2025) e um script de high-pass (agosto de 2025). O motor de áudio, a abertura do programa e o uso no desktop não tiveram uma atualização própria. Flatpak, AUR e COPR continuam empacotando essa árvore. Instalar `jamesdsp` ou `jamesdsp-git` por eles não traz o que está descrito aqui.

Este fork parte do commit `3ea7d73` (`New Crowdin updates`, 1 de outubro de 2026), que é o JamesDSP 2.7.0 com essas traduções e correções pequenas já aplicadas. A atualização foi publicada em 28 de outubro de 2026 no branch `desktop-fixes`. Cada correção, com o nome da função e o que ela passa a fazer, está em [docs/fork-changes.md](docs/fork-changes.md). O botão do equalizador gráfico mora num fork de [ThePBone/GraphicEQWidget](https://github.com/ThePBone/GraphicEQWidget): [pescadordegoiaba/GraphicEQWidget](https://github.com/pescadordegoiaba/GraphicEQWidget), branch `add-frequencie`. O `.gitmodules` aponta para esse endereço, então um clone recursivo já inclui o botão.

Nada disso reescreve o `graphiceq_param` já salvo, copia os modelos de PipeWire ou WirePlumber para `~/.config`, nem reinicia o servidor de áudio. Um perfil de hardware só grava quando a pessoa aperta Apply, e só as chaves da lista permitida.

### Abertura que caía sozinha

Dois travamentos na inicialização com PipeWire tinham a mesma forma. A thread de tempo real e a thread da interface mexiam no estado do DSP ao mesmo tempo.

Com o compander ligado, o áudio entrava em `FFTCompanderProcessSamples` enquanto a interface ainda estava em `FFTCompanderInit`. O mutex da biblioteca não era recursivo, e o processamento não segurava o lock pelo bloco inteiro. Quem ajustava um parâmetro liberava ou reconstruía o FFT debaixo da thread de áudio. O processo morria com segmentation fault.

O segundo aparecia na primeira troca de tamanho de bloco. O motor começa em 128 quadros e o PipeWire em seguida entrega o quantum real, em geral 256. `JamesDSPRefreshConvolutions`, na thread de tempo real, reconstruía os convolvers do equalizador. No mesmo instante `DspHost::update` atualizava o equalizador de tom e o equalizador gráfico na interface. Nenhum dos dois lados segurava `jdsp_lock`. A falha capturada foi `FFTConvolver2x2RefreshImpulseResponse` lendo um segmento que a outra thread já tinha liberado. Quando a thread de tempo real girava nesse estado rasgado, o limite `RLIMIT_RTTIME` do RTKit (200 ms nesta máquina) entregava `SIGKILL`. O shell só imprimia `killed`.

O aviso de impulse vazio do convolver continua aparecendo quando nenhum arquivo está selecionado. Ele não encerra o programa.

O que a biblioteca passou a fazer:

- `m_in_processing` é um mutex recursivo. `JamesDSPSetSampleRate` já o segura e depois chama funções que travam de novo. Um mutex comum entra em deadlock aí. A destruição só acontece depois do unlock.
- O processamento de um bloco, a troca de tamanho de bloco, a reconstrução dos convolvers, o equalizador multimodal, o equalizador gráfico, o crossfeed, o limiter e o pós-ganho usam o mesmo lock.
- O compander volta sem tocar nas amostras enquanto `fftLen` ou `ovpLen` ainda é zero e, nos modos FFT, enquanto `process` é nulo ou `mInputSamplesNeeded` é zero. O modo 3 (gammatone) nunca preenche `process`, então esse ponteiro só é testado nos modos FFT.
- Tempo ou taxa de amostragem inválidos são recusados. O ganho do compander não divide por `fftLen` enquanto ele ainda é zero.
- Cada um dos três convolvers curtos do crossfeed é liberado sozinho. A reconstrução acontece quando os dois convolvers longos faltam. O teste antigo usava OU, destruía e recriava o efeito em todo enable, porque o código só aloca um dos dois longos.
- `setlocale(LC_NUMERIC, "C")` roda dentro de `JamesDSPInit`. A interface já fazia isso. Outros hosts liam decimais no locale do processo.
- A escrita direta do BS2B no modo 0 do crossfeed também segura `jdsp_lock`.

`std::vector<std::string>` substitui o `string` solto em `DspHost`. O `async++` usa `std::invoke_result`, porque `std::result_of` saiu do C++20. O redirecionamento de `printf` inclui o `printf` da CRT antes da macro no MSVC e chama o `vsnprintf` de verdade.

### Equalizador gráfico

Nos modos de 15 e 31 bandas a coluna de frequência fica travada. A linha entre esses pontos é interpolação de decibéis em frequência logarítmica. 24 Hz, 25 Hz e 77,2 Hz não eram uma segunda lista de pontos. Para colocar um desses valores na lista, a barra lateral do equalizador gráfico, acima do AutoEQ, ganhou o botão com o texto `add frequencie`.

O campo aceita de 1 Hz a 30000 Hz, com uma casa decimal, e abre em 1000 Hz. Cancelar não muda nada. Confirmar faz o seguinte:

1. Se o modo não for o variável, passa para variável. Os pontos que já existem continuam, e a coluna de hertz fica editável.
2. Uma frequência a menos de 0,05 Hz de um ponto existente só seleciona essa linha. Dois pontos na mesma frequência fazem a interpolação logarítmica dividir por zero.
3. Nos outros casos o ponto entra na ordem de frequência. O ganho é o da curva naquele hertz, arredondado em 0,01 dB, então a curva não salta.
4. A linha fica selecionada. Hertz e decibéis se editam como qualquer outro ponto do modo variável.

O FIR do equalizador arbitrário tem 8192 pontos. A 48000 Hz a grade fica em cerca de 2,9 Hz. 24 Hz e 25 Hz caem em bins diferentes. 77,0 Hz e 77,2 Hz caem no mesmo bin. A cópia deste widget usada pelo compander esconde a barra lateral, então o botão não aparece ali.

### Esta máquina

O primeiro grupo da página de processamento de áudio se chama "Este computador". Escolher um perfil só preenche o texto de ajuda. Nada é gravado até Apply. O diálogo de confirmação lista as chaves e o padrão é Não. Apply grava só a lista permitida. Qualquer outra chave cancela a gravação inteira. `graphiceq_param` é comparado antes e depois. O equalizador gráfico salvo, inclusive um reforço de grave, permanece.

| Perfil | O que o Apply grava |
| --- | --- |
| Linha verde → EP-2220 | Pós-ganho 0 dB, limiter −1 dB, crossfeed desligado, convolver desligado, bass boost desligado, stereo widener desligado, DDC desligado, compander no modo de transformada 0, liveprog `ep2220Line.eel` |
| VT1705 analógico | Pós-ganho 0 dB, limiter −1 dB, crossfeed desligado |
| HDMI | Nada. A entrada existe para registrar que essa saída não é o conector analógico. |

`ddc_file` não é apagado. A curva do compander e `compander_enable` não são gravados. O modo 0 é o compander STFT (janela 2048, salto 256): 42,7 ms e 5,3 ms a 48 kHz. O modo 2, se for religado, usa janela de 4096 pontos (85,3 ms) e salto de 512 (10,7 ms). O rótulo de latência mostra o quantum do PipeWire e esses números enquanto o compander está ligado. Também diz que um convolver soma o comprimento do impulse, e que no cabo P2 esse atraso é o que chega na caixa.

`ep2220Line.eel` é o script da caixa com woofer de 5,25 pol e driver de 2 pol, alimentada pela saída de linha verde da VIA VT1705 no AUX da EcoPower EP-2220. Não é uma equalização medida da caixa. Por amostra:

1. Bloqueio de DC de um polo a 5 Hz em cada canal.
2. High-pass Butterworth de 4ª ordem a 70 Hz em cada canal, duas seções com Q 0,541196100146197 e 1,306562964876376.
3. Divisão Linkwitz-Riley de 4ª ordem a 100 Hz.
4. Os graves viram mono `(L+R)/2` e somam com os agudos estéreo.

O high-pass de 70 Hz fica nos dois canais, antes da divisão. Colocá-lo só na banda grave mono gira a fase dessa banda, o par Linkwitz-Riley deixa de somar plano e abre um buraco por volta de 85–100 Hz. `highpass70Hz.eel` continua disponível e não é o alvo do perfil. O caminho descrito é o conector verde traseiro, nível de linha, cabo P2, AUX da EP-2220. Não é o P10 de microfone ou instrumento da caixa, e não é Bluetooth. O pós-ganho fica em 0 dB. Não há atenuação extra nem curva de EQ inventada. Bass boost, crossfeed, stereo widener, DDC e convolver saem porque brigam com essa caixa única.

### Latência no conector analógico

Os arquivos em `meta/pipewire/` e `meta/wireplumber/` são modelos. O JamesDSP não os copia e não reinicia o PipeWire nem o WirePlumber.

- `meta/pipewire/10-vt1705.conf` vai para `~/.config/pipewire/pipewire.conf.d/10-vt1705.conf`. Relógio 48000 Hz, quantum padrão 256, mínimo 128, máximo 1024. 256 quadros a 48 kHz são 5,3 ms. O máximo deixa o quantum subir se outros efeitos estalarem.
- `meta/wireplumber/51-vt1705-line.conf` vai para `~/.config/wireplumber/wireplumber.conf.d/51-vt1705-line.conf`. Sintaxe do WirePlumber 0.5. Casa só com `alsa_output.pci-0000_00_1b.0.analog-stereo`, define `audio.rate = 48000`, `node.latency = "256/48000"` e `session.suspend-timeout-seconds = 0`. Não define `api.alsa.period-size`.

`JamesDSPSetSampleRate` liga o resampling interno quando a taxa fica abaixo de 44100 Hz ou acima de 48000 Hz. Travar o conector analógico em 48000 Hz tira esse resampling deste caminho. O estalo de power-save do HDA não é alterado pelo programa. Uma máquina que estala quando o codec acorda pode usar `options snd-hda-intel power_save=0 power_save_controller=N` em `/etc/modprobe.d/snd-hda-intel.conf`. Isso é ajuste do kernel.

### O que mais entrou

- `highpass200Hz.eel` usava um biquad para os dois canais, então eles dividiam estado. Cada canal agora tem o próprio filtro.
- `stftDenoise.eel` passava `sr` ao inicializador. O host registra `srate`. O script usa `srate`.
- Estilos `classic`, `studio`, `midnight`, `paper` e `highcontrast` em `resources/styles/themes/`. A escolha é `AppConfig::VisualTheme`. Um id desconhecido volta para Classic JamesDSP.
- `resources/assets/liveprog/channelBalance.eel` e `resources/assets/irs/CorredHRTF_Surround2_Wet64.wav` (RIFF float IEEE de 4 canais, 48000 Hz). Os dois entram no `resources.qrc` e são extraídos na abertura. Um arquivo que já existe no destino é mantido.

### O que ficou de fora

A porta WASAPI do Windows não foi incorporada. A reescrita de liveprog, a trava de processamento e os ganchos de teste da árvore 051-lab também não. Não há build AVX nem AVX2: a biblioteca DSP usada aqui é C escalar. Não há curva medida nem inventada para a EP-2220. Os perfis não usam o carregador completo de presets, porque um preset substituiria o equalizador gráfico.

<p align="center">
    <a href="https://crowdin.com/project/jdsp4linux" rel="nofollow"><img style="width:140;height:40px" src="https://badges.crowdin.net/badge/light/crowdin-on-dark.png" srcset="https://badges.crowdin.net/badge/light/crowdin-on-dark.png 1x,https://badges.crowdin.net/badge/light/crowdin-on-dark@2x.png 2x" alt="Crowdin" /></a>
    <br/>
    Please help us to <a href="https://crowdin.com/project/jdsp4linux">translate this app</a> on <a href="https://crowdin.com/project/jdsp4linux">Crowdin</a>!
</p>

## Features

* Automatic bass boost
  * Frequency-detecting bass-boost. Automatically sets its own parameters, such as gain, bandwidth, and cut-off frequency, by analyzing the incoming audio stream
* Automatic dynamic range compressor
  * A highly automated multiband dynamic range adjusting effect
* Complex reverberation IIR network (Progenitor 2)
* Interpolated FIR equalizer with flexible bands
* Arbitrary response equalizer (also known as GraphicEQ from EqualizerAPO)
  * AutoEQ database integration (requires network connection)
* Partitioned convolver (Auto segmenting convolution)
  * Supports mono, stereo, full/true stereo (LL, LR, RL, RR) impulse response
* Crossfeed
  * Realistic surround effects
* Soundstage wideness
  * A multiband stereo wideness controller
* ViPER-DDC
  * Perform parametric equalization on audio
  * Create VDC input files using [thepbone/DDCToolbox](https://github.com/thepbone/DDCToolbox)
* Analog modeling
  * An aliasing-free even harmonic generator
* Output limiter
* **Scripting engine: Live programmable DSP**
  * Write your own audio effects using the [EEL2 scripting language](https://github.com/james34602/EEL_CLI)
  * Auto-generate a basic user interface for your script to modify specific parameters/constants without editing the actual code
  * The scripting language has been extended using many DSP-related functions for easy access, for example, spectral processing, constant Q transform, multi-purpose FIR filter designer IIR sub-bands transformation, etc...
  * **This app also includes a custom minimal scripting IDE**:
    * Syntax highlighting
    * Basic code completion
    * Dynamic code outline window
    * Console output support
    * Detailed error messages with inline code highlighting


## PipeWire vs PulseAudio

**Designed for use with PipeWire. PulseAudio is only supported for backward compatibility.**

PipeWire has a much lower latency compared to PulseAudio when injecting audio effects processors into the audio pipeline. 
I'm currently not planning to add more advanced support for PulseAudio clients. Features such as selective app exclusion, changing the target audio device, and similar features will only be available to PipeWire clients.

### Which one am I using?

Follow the instructions below if you don't know which one your Linux distribution is using. If you already know, skip to the 'Install dependencies' section.

Run `LC_ALL=C pactl info | grep "Server Name:"` in your terminal. 

If you are using **Pipewire** the output should look similar to this:
```
Server Name: PulseAudio (on PipeWire 0.3.35)
```

If you are using **Pulseaudio** the output should look exactly like this:
```
Server Name: pulseaudio
```

## Installation

**Decide whether you need to install the PipeWire or PulseAudio version of this app!**

If you don't know which version fits your Linux setup, go to the [PipeWire vs PulseAudio section](#which-one-am-i-using) above.

It is recommended to switch to PipeWire, if possible. JamesDSP's audio backend for PulseAudio is in maintenance-mode; however, it will continue to receive UI-related feature updates.

The installation instructions for the PulseAudio version have been moved to a separate file: [INSTALL_PULSE.md](INSTALL_PULSE.md).

## Installation for PipeWire
This section is dedicated to systems using PipeWire as the audio server. If you are still using PulseAudio, please go [here](INSTALL_PULSE.md).

* [Flatpak](#flatpak)
* [Arch Linux (AUR)](#arch)
* [Fedora/openSUSE](#fedoraopensuse)
* [Build from sources](#build-from-sources)

### Flatpak

Universal binary packages for all distros.

The recommended **Pipewire version** is available for download on FlatHub: https://flathub.org/apps/me.timschneeberger.jdsp4linux
```
flatpak install me.timschneeberger.jdsp4linux
```

<a href='https://flathub.org/apps/me.timschneeberger.jdsp4linux'><img width='240' alt='Download on Flathub' src='https://dl.flathub.org/assets/badges/flathub-badge-en.png'/></a>


If you are still using **PulseAudio**, you need to download the legacy package from my personal repository:
```
sudo flatpak remote-add --if-not-exists thepbones-repo https://raw.githubusercontent.com/ThePBone/flatpak-repo/main/thepbone.flatpakrepo
flatpak install me.timschneeberger.jdsp4linux.pulse
```

> [!IMPORTANT]
> Flatpaks are sandboxed. This application can only access `~/.var/app/me.timschneeberger.jdsp4linux/` by default.

### Arch
[AUR packages](https://aur.archlinux.org/packages/?O=0&K=jamesdsp) are available:

* Stable version

   ![AUR version](https://img.shields.io/aur/version/jamesdsp) ![AUR version](https://img.shields.io/aur/votes/jamesdsp) ![AUR version](https://img.shields.io/aur/maintainer/jamesdsp) ![AUR version](https://img.shields.io/aur/last-modified/jamesdsp)
   ```
   yay -S jamesdsp
   ```

* Development version

   ![AUR version](https://img.shields.io/aur/version/jamesdsp-git) ![AUR version](https://img.shields.io/aur/votes/jamesdsp-git) ![AUR version](https://img.shields.io/aur/maintainer/jamesdsp-git) ![AUR version](https://img.shields.io/aur/last-modified/jamesdsp-git)
   ```
   yay -S jamesdsp-git
   ```

### Fedora/openSUSE

Package maintained by [@theAeon](https://github.com/theAeon) on [Fedora COPR](https://copr.fedorainfracloud.org/coprs/arrobbins/JDSP4Linux/).
Built for Fedora 34/35/Rawhide and OpenSUSE Tumbleweed.

```
yum copr enable arrobbins/JDSP4Linux && yum update && yum install JamesDSP
```

If you are still using PulseAudio with your Fedora/openSUSE installation, refer to the '[Build from sources](#build-from-sources)' section below instead.

### Build from sources

Build instructions are available in the [BUILD.md](BUILD.md) file.

## Scripting & IPC APIs

Since version 2.5.0, this app supports IPC via D-Bus and is also configurable via a CLI.

### Remote control via CLI
You can list all supported commands using `jamesdsp --help`. 
Currently, these commands for remote-controlling JamesDSP's audio engine are available:
```
  --is-connected                           Check if JamesDSP service is active. Returns exit code 1 if not. (Remote)
  --list-keys                              List available audio configuration keys (Remote)
  --get <key>                              Get audio configuration value (Remote)
  --set <key=value>                        Set audio configuration value (format: key=value) (Remote)
  --load-preset <name>                     Load preset by name (Remote)
  --save-preset <name>                     Save current settings as preset (Remote)
  --delete-preset <name>                   Delete preset by name (Remote)
  --list-presets                           List presets (Remote)
  --status                                 Show status (Remote)
  --list-devices                           List audio devices (Remote)
  --list-preset-rules                      List preset rules (Remote)
  --set-preset-rule <deviceId=presetName>  Add/modify preset rule (Remote)
  --delete-preset-rule <deviceId>          Delete preset rule (Remote)
```
The options should be fairly self-explanatory. For example, `jamesdsp --set reverb_enable=true` would enable the reverberation setting. Have a look at the audio configuration file at `~/.config/jamesdsp/audio.conf` to learn more about possible setting keys and their syntax.

> [!NOTE]
> These commands try to connect to an active JamesDSP instance. If no instance is currently online, they will fall back to modifying the audio configuration file directly on disk. The `--is-connected` option can be used to check whether one is currently online.

### D-Bus IPC

This app also exposes a D-Bus service on the session bus, which can be used by other developers or users:

Service name: `me.timschneeberger.jdsp4linux`
* GUI-related interface:
  * Path name: `/jdsp4linux/gui`
  * Interface name: `me.timschneeberger.jdsp4linux.Gui`
* Audio service-related interface:
  * Path name: `/jdsp4linux/service`
  * Interface name: `me.timschneeberger.jdsp4linux.Service`

If you want to test it out, you can use an app like [D-Feet](https://wiki.gnome.org/Apps/DFeet) to interact with the D-Bus services.

The D-Bus introspection XML is available here: https://github.com/Audio4Linux/JDSP4Linux/blob/master/src/utils/dbus/manifest.xml.

## Troubleshooting
* JamesDSP is randomly killed by the kernel or closes by itself
  * The processing thread of the app may have exceeded the maximum amount of CPU time allowed for a real-time thread on your system.
  * To fix: install the `realtime-priorities` package, add your user to the `realtime` group, and re-login. (see [issue #155](https://github.com/Audio4Linux/JDSP4Linux/issues/155#issuecomment-1835866017))

* My volume control is not working anymore
  * Don't set the virtual JamesDSP device as the default audio output device. The virtual device has no audio volume controls and should never be used directly. Please set your actual speakers/headphones as the default output device instead.

* Crackling audio
  * Your CPU may be too slow to process the audio sample in time; try to disable some effects (especially resource-hungry ones like the convolver)
  * Set JamesDSP's process to real-time or high priority using a task manager of your choice
  * [Pipewire] Try out the workaround mentioned in [issue #47](https://github.com/Audio4Linux/JDSP4Linux/issues/47)

## Screenshots

<p align="center">
   <img alt="Screenshot" width="702" src="https://github.com/Audio4Linux/JDSP4Linux/blob/master/meta/screenshot_presets.png?raw=true">  
   <img alt="Screenshot" width="702" src="https://github.com/Audio4Linux/JDSP4Linux/blob/master/meta/screenshot_eel.png?raw=true"> 
   <img alt="Screenshot" width="702" src="https://github.com/Audio4Linux/JDSP4Linux/blob/master/meta/screenshot_graphiceq.png?raw=true"> 
   <img alt="Screenshot" width="702" src="https://github.com/Audio4Linux/JDSP4Linux/blob/master/meta/screenshot_ide.png?raw=true">
</p>

## Contributors

* [James Fung](https://github.com/james34602) - Developer of the core library ['libjamesdsp'](https://github.com/james34602/JamesDSPManager/tree/master/Main)
* [yochananmarqos](https://github.com/yochananmarqos) - AUR packages
* [theAeon](https://github.com/theAeon) - RPM packages
* PipeWire/Pulse implementation based on [EasyEffects](https://github.com/wwmm/EasyEffects) by [Wellington Wallace](https://github.com/wwmm)

### Translators

<!-- CROWDIN-CONTRIBUTORS-START -->
<table>
  <tr>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/ThePBone"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15683553/medium/d13428d1e0922bc2069500aef57d1459.png" />
        <br />
        <sub><b>Tim Schneeberger (ThePBone)</b></sub></a>
      <br />
      <sub><b>7404 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/Kazevic"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15680393/medium/fd2c52453ee1d86f41406cff9346d74d.png" />
        <br />
        <sub><b>Kauã Azevedo (Kazevic)</b></sub></a>
      <br />
      <sub><b>3174 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/Camellan"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/13410766/medium/b4019516b3323e817b7e77712961de69_default.png" />
        <br />
        <sub><b>Camellan</b></sub></a>
      <br />
      <sub><b>3152 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/KatieFrogs"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/14450708/medium/bb568293d8b26daa3f7f323dbc9d69d0.png" />
        <br />
        <sub><b>Katie Frogs (KatieFrogs)</b></sub></a>
      <br />
      <sub><b>2955 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/catvinyl"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15923209/medium/87c1ec8379dee277ba0bd42e1d8206f2.png" />
        <br />
        <sub><b>catvinyl</b></sub></a>
      <br />
      <sub><b>2352 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/Gokwu"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15975377/medium/7be6218dc0f81f4f2dc8418ea983bd9e.png" />
        <br />
        <sub><b>Choi Jun Hyeong (Gokwu)</b></sub></a>
      <br />
      <sub><b>2321 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/bl4rnk"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16644221/medium/f77268f6e88c039a3f8d15b3eb472565.jpeg" />
        <br />
        <sub><b>bl4rnk</b></sub></a>
      <br />
      <sub><b>1713 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/adem4ik"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/12721323/medium/76216572f2d8eae3b1dddf44eca8a7e5.png" />
        <br />
        <sub><b>Andrei Stepanov (adem4ik)</b></sub></a>
      <br />
      <sub><b>1159 words</b></sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/ldavid8000"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/13511452/medium/0ce6a572593d9697722ec0420156c4d1.jpg" />
        <br />
        <sub><b>Dávid László (ldavid8000)</b></sub></a>
      <br />
      <sub><b>818 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/HoleHolo"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/14737398/medium/7d120eb168560837ca53bd0f189be716.png" />
        <br />
        <sub><b>HoleHolo</b></sub></a>
      <br />
      <sub><b>814 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/tfkzptzt"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16861075/medium/80c6de4c9945226dd698f55e846842e7.jpeg" />
        <br />
        <sub><b>同福客栈跑堂蘸糖 (tfkzptzt)</b></sub></a>
      <br />
      <sub><b>775 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/unsignedchar256"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16377124/medium/7826d5b09ad36a1dcb403067b0c5a67e_default.png" />
        <br />
        <sub><b>unsigned char (unsignedchar256)</b></sub></a>
      <br />
      <sub><b>730 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/Yuletsi"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16507043/medium/f3e11341009b2302a80f9e62be303758.png" />
        <br />
        <sub><b>Yuletsi Lara Maceo (Yuletsi)</b></sub></a>
      <br />
      <sub><b>663 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/mefsaal"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/13221907/medium/c0b751a37f076028f7271b00392736aa.jpg" />
        <br />
        <sub><b>Gabriel Cabrera Davila (mefsaal)</b></sub></a>
      <br />
      <sub><b>652 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/seqfault"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15878639/medium/bf4af8eeefcd4c065fd867a7ad16994b.jpeg" />
        <br />
        <sub><b>NullPointerException (seqfault)</b></sub></a>
      <br />
      <sub><b>1151 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/mariachini"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/13113640/medium/99ff648dd8f28efebdce9713cee1b9c3.png" />
        <br />
        <sub><b>mariachini</b></sub></a>
      <br />
      <sub><b>373 words</b></sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/token0"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/17410814/medium/2b93212ecd91d1ad45cead0226737e90.jpeg" />
        <br />
        <sub><b>token0</b></sub></a>
      <br />
      <sub><b>300 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/Regu_Miabyss"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15317124/medium/9099ec758c209481e79cd9b23b7f6210.png" />
        <br />
        <sub><b>Regu_Miabyss</b></sub></a>
      <br />
      <sub><b>283 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/gkpiccoli"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15512952/medium/57e54eba4de46714593f1d0dcbcdfa3a.jpeg" />
        <br />
        <sub><b>Gustavo Kureck Piccoli (gkpiccoli)</b></sub></a>
      <br />
      <sub><b>280 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/zhanghua000"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15906451/medium/db06f53e9b83801ec99389ab759b1e48.png" />
        <br />
        <sub><b>Noob Zhang (zhanghua000)</b></sub></a>
      <br />
      <sub><b>240 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/etiennec78"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15912311/medium/49bacd7fcfc807ea0c5f97127af5bb26.jpeg" />
        <br />
        <sub><b>Etienne C. (etiennec78)</b></sub></a>
      <br />
      <sub><b>232 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/kzsuser"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16189758/medium/b0b9640e2be596e641f458345859f5dd_default.png" />
        <br />
        <sub><b>kzsuser</b></sub></a>
      <br />
      <sub><b>221 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/kanium"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16501577/medium/c73db838b312ee88a0f455b96be1301c.jpeg" />
        <br />
        <sub><b>kanium</b></sub></a>
      <br />
      <sub><b>200 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/olivertzeng"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15654475/medium/042534e02b191eeb6f634f8fe4731a2f.jpg" />
        <br />
        <sub><b>Oliver Tzeng (olivertzeng)</b></sub></a>
      <br />
      <sub><b>200 words</b></sub>
    </td>
  </tr>
  <tr>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/AnClark"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15932859/medium/621b49aa22122e6ca25fc819eac5e96f.jpeg" />
        <br />
        <sub><b>AnClark Liu (AnClark)</b></sub></a>
      <br />
      <sub><b>173 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/deproocho"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/14966313/medium/71b861af9d1e455f743c1002b4d24622.jpeg" />
        <br />
        <sub><b>Bruh (deproocho)</b></sub></a>
      <br />
      <sub><b>171 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/so1ar"><img alt="logo" style="width: 64px" src="https://i2.wp.com/crowdin.com/images/user-picture.png?ssl=1" />
        <br />
        <sub><b>so1ar</b></sub></a>
      <br />
      <sub><b>126 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/coldified_"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16382586/medium/27690602b0b2014444ad1346007a6734.png" />
        <br />
        <sub><b>Inche Hwang (coldified_)</b></sub></a>
      <br />
      <sub><b>117 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/SubaruLenz"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/16808355/medium/8f4aecf47a100ae6e3c9934e13ec8f65.jpeg" />
        <br />
        <sub><b>NguyenTrungKien (SubaruLenz)</b></sub></a>
      <br />
      <sub><b>114 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/abernes"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/17020080/medium/92670d834cd5aad405e2d63eba13405c.png" />
        <br />
        <sub><b>Dr. Alejandro Ruiz Bernés (abernes)</b></sub></a>
      <br />
      <sub><b>75 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/james34602"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/15764811/medium/9c166ce647706bbfc568c13763b8a50d.jpeg" />
        <br />
        <sub><b>James Fung (james34602)</b></sub></a>
      <br />
      <sub><b>72 words</b></sub>
    </td>
    <td align="center" valign="top">
      <a href="https://crowdin.com/profile/loccun"><img alt="logo" style="width: 64px" src="https://crowdin-static.cf-downloads.crowdin.com/avatar/17363768/medium/189f23739bcbc17bc6d7e2cd6ebb1c5e.png" />
        <br />
        <sub><b>Loc Huynh (loccun)</b></sub></a>
      <br />
      <sub><b>68 words</b></sub>
    </td>
  </tr>
</table><a href="https://crowdin.com/project/jdsp4linux" target="_blank">Translate in Crowdin 🚀</a>
<!-- CROWDIN-CONTRIBUTORS-END -->

## License

This project is licensed under [GPLv3](LICENSE).

The project makes use of [Papirus icons](https://github.com/PapirusDevelopmentTeam/papirus-icon-theme) licensed under [GPLv3](LICENSE).

```
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR 
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
