# Changes in this fork

Base commit: `3ea7d73` (`New Crowdin updates`, JamesDSP for Linux 2.7.0) from [Audio4Linux/JDSP4Linux](https://github.com/Audio4Linux/JDSP4Linux).

This fork does not replace a saved `graphiceq_param`, does not install PipeWire or WirePlumber drop-ins, and does not restart the audio server. Hardware profiles write only the keys listed below, and only when the user presses Apply.

The graphic EQ button lives in a fork of [ThePBone/GraphicEQWidget](https://github.com/ThePBone/GraphicEQWidget). `.gitmodules` points at that fork so a recursive clone contains the button.

## Startup crashes

Two startup failures had the same shape. The PipeWire realtime thread and the GUI thread both mutated DSP state.

The first failure is a segmentation fault in the compander. With the compander enabled, the audio thread can be inside `FFTCompanderProcessSamples` while the GUI thread is inside `FFTCompanderInit`. The library mutex was not recursive, and the process function did not hold it for the whole block. Setters could free or rebuild FFT state under the audio thread.

The second failure shows up on the first block-size change (the engine starts at 128 frames and PipeWire then delivers the real quantum, often 256). `JamesDSPRefreshConvolutions` runs on the realtime thread and rebuilds the equalizer convolvers. At the same time `DspHost::update` refreshes the tone equalizer and the graphic EQ on the GUI thread. Neither side held `jdsp_lock`. The captured fault was `FFTConvolver2x2RefreshImpulseResponse` reading a segment pointer that the other thread had already freed. When the realtime thread instead spun on the torn state, RTKit's `RLIMIT_RTTIME` (200 ms on this setup) delivered `SIGKILL` and the shell only printed `killed`.

The empty-impulse warning from the convolver is not fatal. It means no impulse file is selected.

Fixes in `libjamesdsp`:

- `m_in_processing` is a recursive mutex. `JamesDSPSetSampleRate` already holds it and then calls functions that lock again. A normal mutex deadlocks there. The mutex is destroyed only after it has been unlocked.
- `JamesDSPProcess` and the benchmark process function hold the lock for the whole block.
- `JamesDSPRefreshConvolutions`, `JamesDSPReallocateBlock`, `MultimodalEqualizerAxisInterpolation`, `MultimodalEqualizerEnable`, `ArbitraryResponseEqualizerStringParser`, and `ArbitraryResponseEqualizerEnable` hold the same lock. `GetFilter` for the graphic EQ is inside the lock, not only the free-and-load that follows it.
- `CompressorProcess` returns without touching samples when `fftLen` or `ovpLen` is still zero, and, for the FFT modes, when `process` is null or `mInputSamplesNeeded` is zero. Mode 3 (gammatone) never assigns `process`, so that pointer is checked only for the FFT modes. An enabled compander used to enter `FFTCompanderProcessSamples` before `FFTCompanderInit`. With `mInputSamplesNeeded == 0` the copy loop never advances, and a null `process` call faults.
- `CompressorSetParam` refuses a non-finite or non-positive time constant and a non-positive sample rate. `CompressorSetGain` returns when `fftLen` is still zero, because that path divides by it. `CompressorEnable` calls `SetGain` only after init has set `fftLen`.
- `CrossfeedDestructor` frees each of the three short convolvers on its own. The old destructor freed `conv[1]` and `conv[2]` whenever `conv[0]` was set. `CrossfeedEnable` rebuilds when both long convolvers are missing. The old test used OR, so every later enable destroyed and rebuilt the effect, because the code allocates only one of the two long convolvers.
- `CrossfeedDisable`, `CrossfeedChangeMode`, the limiter coefficient write, and post-gain take the lock.
- `setlocale(LC_NUMERIC, "C")` runs inside `JamesDSPInit`, so decimal parsing does not follow the process locale. The GUI already did this. Other hosts did not.
- The direct BS2B write in `DspHost::updateCrossfeed` (mode 0) holds `jdsp_lock`.

`std::vector<std::string>` replaces an unqualified `string` in `DspHost`. `async++` uses `std::invoke_result` because `std::result_of` is removed in C++20. The printf redirect header includes the CRT `printf` before the macro on MSVC, and the implementation calls the real `vsnprintf`.

## Liveprog scripts that were already shipped

`highpass200Hz.eel` used one biquad for both channels, so the channels shared state. Each channel now has its own filter.

`stftDenoise.eel` passed `sr` into the initializer. The host registers `srate`. The script now uses `srate`.

## Assets

These files are registered in `resources/resources.qrc` and extracted on startup. An existing file is left in place.

- `resources/assets/liveprog/channelBalance.eel`
- `resources/assets/irs/CorredHRTF_Surround2_Wet64.wav` (4-channel IEEE-float RIFF, 48000 Hz)
- Stylesheets `classic`, `studio`, `midnight`, `paper`, and `highcontrast` under `resources/styles/themes/`. The choice is `AppConfig::VisualTheme`. Unknown ids fall back to Classic JamesDSP. The stylesheet is appended in `StyleHelper`.

## This computer

The first group on the audio-processing settings page is "Este computador". Choosing a profile only fills the help text. Nothing is written until Apply. The confirmation dialog lists the keys and defaults to No. Apply writes only the allow-list. Any other key aborts the whole apply and does not save. `graphiceq_param` is compared before and after. The saved graphic EQ, including a bass boost, stays as it is.

| Profile | What Apply writes |
| --- | --- |
| Linha verde → EP-2220 | Post-gain 0 dB, limiter −1 dB, crossfeed off, convolver off, bass boost off, stereo widener off, DDC off, compander transform mode 0, liveprog `ep2220Line.eel` |
| VT1705 analógico | Post-gain 0 dB, limiter −1 dB, crossfeed off |
| HDMI | Nothing. The entry documents that this path is not the analog jack. |

`ddc_file` is not cleared. The compander curve and `compander_enable` are not written. Mode 0 is the STFT compander (window 2048, hop 256): 42.7 ms and 5.3 ms at 48 kHz. Mode 2, if the user turns it back on, is a 4096-point window (85.3 ms) and a hop of 512 (10.7 ms). The latency label shows the PipeWire quantum and those numbers while the compander is enabled. It also says that a convolver adds the length of its impulse, and that on the P2 cable this delay is what reaches the speaker.

`ep2220Line.eel` is the cabinet script for one box with a 5.25 inch woofer and a 2 inch driver, fed from the green line-out of the VIA VT1705 into the EP-2220 aux jack. It is not a measured speaker EQ. Per sample it does this:

1. One-pole DC block at 5 Hz on each channel.
2. A 4th-order Butterworth high-pass at 70 Hz on each channel, two sections with Q 0.541196100146197 and 1.306562964876376.
3. A Linkwitz-Riley 4th-order split at 100 Hz.
4. The lows become mono `(L+R)/2` and sum with the stereo highs.

The 70 Hz high-pass is on both channels before the split. Putting it only on the mono low band rotates that band's phase, so the Linkwitz-Riley pair no longer sums flat and a hole opens around 85–100 Hz. `highpass70Hz.eel` remains available and is not the profile target. `3bandSplitting.eel` is not deleted. The script is copied beside the other liveprog files only when Apply runs, and an existing destination file is kept.

The path this profile describes is the rear green jack, line level, P2 cable, EP-2220 aux. It is not the speaker's P10 mic or instrument jack, and it is not Bluetooth. Post-gain stays at 0 dB. There is no extra attenuation and no invented EQ curve. Bass boost, crossfeed, the stereo widener, DDC, and the convolver are turned off because they fight this single cabinet.

## PipeWire drop-ins

These files are templates. JamesDSP does not copy them and does not restart PipeWire or WirePlumber.

- `meta/pipewire/10-vt1705.conf` goes to `~/.config/pipewire/pipewire.conf.d/10-vt1705.conf`. Clock 48000 Hz, default quantum 256, minimum 128, maximum 1024. 256 frames at 48 kHz is 5.3 ms. The maximum lets the quantum rise if other effects crackle.
- `meta/wireplumber/51-vt1705-line.conf` goes to `~/.config/wireplumber/wireplumber.conf.d/51-vt1705-line.conf`. WirePlumber 0.5 syntax. It matches only `alsa_output.pci-0000_00_1b.0.analog-stereo`, sets `audio.rate = 48000`, `node.latency = "256/48000"`, and `session.suspend-timeout-seconds = 0`. It does not set `api.alsa.period-size`.

`JamesDSPSetSampleRate` turns on the internal resampler when the rate is below 44100 Hz or above 48000 Hz. Locking the analog jack at 48000 Hz keeps that resampler out of this path.

The HDA power-save click is not changed from the application. A machine that clicks when the codec wakes can set `options snd-hda-intel power_save=0 power_save_controller=N` in `/etc/modprobe.d/snd-hda-intel.conf`. That is a kernel setting, not something this program writes.

## Graphic EQ

On the graphic EQ sidebar, above AutoEQ, the button is labeled `add frequencie`.

The 15-band and 31-band modes lock the frequency column. The line between those points is log-frequency interpolation of decibels, not a second set of editable points. The button opens a text field, starting at 1000. `77.2` and `77,2` are both accepted. Cancel changes nothing. A value outside 1 Hz to 30000 Hz changes nothing. Accept does this:

1. If the mode is not variable, it switches to variable. The existing points stay, and the frequency column becomes editable.
2. The typed frequency is appended as its own point. Every frequency and gain that was already in the list is copied unchanged.
3. The whole list is sorted from the lowest frequency to the highest and the table is rebuilt in that order.
4. A frequency within 0.05 Hz of an existing point selects that row and does not change its number. Two points on the same frequency make the log interpolation divide by zero.
5. The new point's gain is the current curve at that frequency, rounded to 0.01 dB, so the curve does not jump. The row is selected. Hertz and decibels are then edited like any other variable-mode point.

The FIR used by the arbitrary equalizer is 8192 points. At 48000 Hz the grid is about 2.9 Hz. 24 Hz and 25 Hz land in different bins. 77.0 Hz and 77.2 Hz land in the same bin.

The compander's copy of this widget hides the sidebar, so the button is not shown there.

## Left out on purpose

- The Windows WASAPI port was not merged.
- The liveprog rewrite, processing gate, and test hooks from the 051-lab tree were not merged.
- No AVX or AVX2 build. The DSP library used here is scalar C.
- No measured or invented correction curve for the EP-2220.
- Profiles do not use the full preset loader. A preset replace would overwrite the graphic EQ.
