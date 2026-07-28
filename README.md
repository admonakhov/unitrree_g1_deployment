# Флаги сборки

В этом репозитории поддерживается выбор пакета ONNX Runtime во время конфигурации CMake для:

- `robots/g1_29dof`

## Поддерживаемый флаг

- `USE_ONNXRUNTIME_AARCH64`
  - По умолчанию `OFF`
  - Когда `OFF`, сборка использует `thirdparty/onnxruntime-linux-x64-1.22.0`
  - Когда `ON`, сборка использует `thirdparty/onnxruntime-linux-aarch64-1.23.2`

## Примеры сборки

### Сборка x64

```bash
cd robots/g1_29dof/build
cmake ..
make
```

### Сборка aarch64

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

## Использование для деплоя

Ниже приведён базовый порядок запуска контроллера и подачи команд скорости.

### G1 29DoF

1. Соберите проект:

```bash
cd robots/g1_29dof/build
cmake ..
make
```

2. Запустите контроллер:

```bash
cd robots/g1_29dof/build
./g1_ctrl
```

3. Переведите робота в рабочие состояния с пульта:
   - `L2 + Up` для перехода в `FixStand`
   - `R1 + X` для перехода в `Velocity`
