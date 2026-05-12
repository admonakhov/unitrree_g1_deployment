# Build Flags

This repository supports selecting the ONNX Runtime package at CMake configure time for:

- `robots/g1_29dof`
- `robots/a1_23dof`

## Supported Flag

- `USE_ONNXRUNTIME_AARCH64`
  - `OFF` by default
  - When `OFF`, the build uses `thirdparty/onnxruntime-linux-x64-1.22.0`
  - When `ON`, the build uses `thirdparty/onnxruntime-linux-aarch64-1.23.2`

## Build Examples

### x64 build

```bash
cd robots/g1_29dof/build
cmake ..
make
```

```bash
cd robots/a1_23dof/build
cmake ..
make
```

### aarch64 build

```bash
cd robots/g1_29dof/build
cmake .. -DUSE_ONNXRUNTIME_AARCH64=ON
make
```

```bash
cd robots/a1_23dof/build
cmake .. -DUSE_ONNXRUNTIME_AARCH64=ON
make
```
