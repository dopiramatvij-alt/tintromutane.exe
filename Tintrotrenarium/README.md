# Tintrotrenarium

A Windows C++ desktop-effects simulator with full-screen GDI pixel effects and procedural bytebeat audio.

## Run

The application shows a Yes/No confirmation before starting. Press **Esc** to stop. Twelve ten-second stages play once, then the rotating full-screen kaleidoscope stays as the final effect from 120 seconds onward. Its thirteenth bytebeat repeats every five seconds until Esc is pressed. There is no automatic timeout.

**Flashing visuals and audio:** the effects cover the desktop and can interfere with normal desktop use. Save your work before starting and avoid running it if flashing images affect you.

The simulator does not install persistence, change the registry, modify user files, or require administrator privileges. On exit it stops the audio, releases its drawing resources, and requests a desktop repaint. Some windows may need to be refreshed to remove remaining visual artifacts.

## New effects and paired audio

Each new effect covers the full virtual desktop. Its ten-second bytebeat starts with the effect; each sample rate has a separate entry in `BYTEBEAT_RATES`.

| Time | Effect | Default sample rate |
| --- | --- | --- |
| 60-70 s | RGB split | 11,025 Hz |
| 70-80 s | Vortex | 22,050 Hz |
| 80-90 s | Mosaic lenses | 32,000 Hz |
| 90-100 s | Mirrored tunnel | 16,000 Hz |
| 100-110 s | Plasma bloom | 22,050 Hz |
| 110-120 s | Full-screen melt | 22,050 Hz |
| 120 s onward | Final kaleidoscope | Existing kaleidoscope setting |

The full-screen melt stage progressively displaces desktop pixels into downward flows, sideways smears and overlapping trails across the whole virtual desktop. Each completed frame feeds into the next, so the deformation accumulates instead of appearing in isolated rectangles. Its sample rate is editable at `BYTEBEAT_RATES[11]`.

## Build in Visual Studio

1. Install Visual Studio with the **Desktop development with C++** workload and a Windows SDK. This project uses the **v145** toolset from Visual Studio 2026.
2. Open `Tintrotrenarium.sln`.
3. Choose **Release | x64** or **Debug | x64**, then select **Build Solution**.

The executable is written to `outputs/x64/Release/gdi_chaos.exe` or `outputs/x64/Debug/gdi_chaos.exe`.

To build from an x64 Developer Command Prompt:

```bat
msbuild Tintrotrenarium.sln /m /p:Configuration=Release /p:Platform=x64
```

## Change the audio

Plasma bloom (100-110 seconds) uses `9*(t*((t>>9|t>>13)&15)&16)` at 22,050 Hz. Its editable rate is `BYTEBEAT_RATES[10]`.

The thirteenth track uses `(t*t/(1+(t>>9&t>>8))&128)` at 8,000 Hz by default; its sample counter resets each time the kaleidoscope starts. Edit `BYTEBEAT_RATES` near the top of `outputs/gdi_chaos.cpp` to set each track's sample rate in Hz, then rebuild. Bytebeat formulas are in `BytebeatAudio::Play`; the second track uses `SecondBytebeatSample` so its decimal arithmetic and negative samples match unsigned Bytebeat playback.

The second track was supplied through this [Dollchan composer link](https://dollchan.net/bytebeat/#4AAAA+kXT0CjRMtJUNTYyN7PQ1NIozszTAIlABfSNDAz0DDQ1AQ), which specifies Bytebeat mode at 8,000 Hz.

## Project files

- `outputs/gdi_chaos.cpp`: application source.
- `Tintrotrenarium.sln`: Visual Studio solution.
- `Tintrotrenarium.vcxproj` and `.filters`: C++ project configuration.

Visual Studio caches, user-specific settings, executables, debugging symbols, and temporary build files are excluded from Git.
