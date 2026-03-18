# pbAudioPlayer

A lightweight, fast-launching audio player built with C++/JUCE 8.

![pbAudioPlayer](https://img.shields.io/badge/C%2B%2B-JUCE%208-blue)

## Features

- **Instant startup** – Window appears in under 100ms, audio device initializes in background
- **Waveform display** – Color-coded playback position (orange = played, turquoise = remaining)
- **Spectrogram** – FFT-based jet-colormap spectrogram with crossfade slider
- **Peak meter** – Multi-channel dB-segmented peak meter with peak hold
- **Loudness metering** – EBU R128: Integrated (I), Momentary (M), Short-term (S), LRA
- **Loop playback** – Right-click drag to select loop region
- **Loop export** – Ctrl+Left click to drag-and-drop loop region as WAV file
- **All channel display** – View each audio channel separately
- **Load to memory** – Stream first, seamlessly swap to RAM (releases file handle)
- **Drag & drop** – Drop audio files onto the window
- **Settings persistence** – All settings saved to XML
- **Bilingual UI** – English / Japanese switchable from Settings > Language

## Supported Formats

WAV, MP3, FLAC, OGG, AIFF

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| `Space` | Play / Pause |
| `Ctrl+Space` | Stop (or seek to loop start) |
| `Escape` | Clear loop selection |

## Build

Requires [JUCE 8](https://juce.com/) at `C:/JUCE` and Visual Studio 2022.

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

## License

MIT
