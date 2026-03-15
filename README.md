# Organya Playback Plugin

Plays Organya music files (.org) from the Cave Story engine using
Strultz's organya.h single-header C89 library.

## Supported Formats

| Format | Extension | Magic | Description |
|--------|-----------|-------|-------------|
| Org-01 | .org | "Org-01" | Original format |
| Org-02 | .org | "Org-02" | Extended format |
| Org-03 | .org | "Org-03" | Latest format |

## Library Source

| Library | Source | Commit |
|---------|--------|--------|
| organya.h | https://github.com/Strultz/organya.h | 59fe23704e53f5acff7a319964215f41d61ee652 |

## Soundbank

Organya files require a wavetable soundbank (.wdb) containing 100 melody
waveforms and 42 percussion samples for authentic Cave Story playback.
Since the original soundbank cannot be redistributed, this plugin generates
basic waveforms procedurally (sine, triangle, square, sawtooth, harmonic
blends). Melody channels will produce sound; percussion channels will be
silent without a proper .wdb file.

## Duration

Organya songs loop indefinitely. The plugin enforces a default duration
of 180 seconds (3 minutes).

## License

See [LICENSE](LICENSE) (BSD Zero Clause License).
