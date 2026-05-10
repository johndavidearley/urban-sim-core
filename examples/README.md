# Example: Basic headless simulation

This directory contains example configurations and scripts for running the simulation.

## Running the CLI

```bash
cd build

# Basic run: 64x64 map, 1000 ticks
./bin/UrbanSimCore-cli --size 64 --ticks 1000 --seed 42

# Large city: 256x256 map
./bin/UrbanSimCore-cli --size 256 --ticks 5000 --seed 12345

# Help
./bin/UrbanSimCore-cli --help
```

See [../docs/MVP_SPEC.md](../docs/MVP_SPEC.md) for more details on features.
