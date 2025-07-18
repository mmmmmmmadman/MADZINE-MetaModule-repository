# MADZINE - MetaModule Edition

A comprehensive collection of music modules ported to the MetaModule platform, featuring sequencers, synthesizers, effects, and utilities for electronic music production.

## Original VCV Rack Version

This is the MetaModule port of the MADZINE plugin. For the original VCV Rack version, visit: [MADZINE VCV Rack](https://github.com/mmmmmmmadman/MADZINE)

## Modules

### Sequencers & Rhythm
- **SwingLFO** - Dual-waveform LFO with swing and shape control
- **EuclideanRhythm** - Three-track Euclidean rhythm generator with CV control and clock division
- **MADDY** - Integrated sequencer with swing clock and 3-track Euclidean rhythm generator
- **PPaTTTerning** - Pattern-based CV sequencer with style and density control

### Drum Machines & Synthesis
- **TWNC** - Dual-track Euclidean rhythm generator for techno sequences with audio synthesis
- **TWNCLight** - Lightweight dual-track Euclidean rhythm generator with envelope outputs
- **TWNC2** - Advanced drum machine with kick, snare, and hi-hat synthesis with sidechain ducking
- **Pinpple** - Ping filter hi-hat synthesizer with dynamic FM modulation

### Envelopes & Modulation
- **ADGenerator** - Attack/Decay envelope generator with bandpass filtering
- **QQ** - 3-track S-Curve decay trigger envelope generator with CV control and waveform scope

### Utilities
- **Observer** - 8-track color scope module for waveform visualization

## System Requirements

- **Platform**: MetaModule hardware
- **SDK**: MetaModule Plugin SDK
- **Compiler**: ARM GCC 12.3.1 (for macOS Apple Silicon)
- **Build System**: CMake 3.22+

## Building

```bash
# Clone the repository
git clone https://github.com/mmmmmmmadman/MADZINE-MetaModule-repository.git
cd MADZINE-MetaModule-repository

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .
```

## Installation

After building, the plugin will be available in your MetaModule plugins directory.

## Features

- **High-quality audio processing** optimized for MetaModule
- **Comprehensive CV control** for all major parameters
- **Visual feedback** with integrated scopes and lights
- **Memory-safe implementation** using fixed arrays instead of dynamic allocation
- **Eurorack-compatible** voltage standards

## License

GPL-3.0 License - see [LICENSE](LICENSE) file for details.

## Support

- **Issues**: [GitHub Issues](https://github.com/mmmmmmmadman/MADZINE-MetaModule-repository/issues)
- **Donations**: [Patreon](https://www.patreon.com/c/madzinetw)
- **Email**: madzinetw@gmail.com

## Version

Current version: 2.1.6
