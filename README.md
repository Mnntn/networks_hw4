# MyDrive

`MyDrive` — клиент-серверное файловое хранилище на `C++ / Boost.Asio` с custom TCP protocol, параллельной передачей файлов и режимами `DMA / non-DMA`.

## Что реализовано
- Control connection для `Hello`, `FileList`, `SyncPlan`, `SyncComplete`
- Отдельные file-transfer connections для передачи содержимого файлов
- Параллельные upload'ы с ограничением `max_connections` в диапазоне `1..32`
- `non-DMA` передача через буферизованные `async_write`
- `DMA` путь для Linux через `sendfile`
- Проверка checksum, временные `.part` файлы, atomic replace, удаление лишних файлов на сервере после успешной синхронизации

## Структура
- `src/common` — протокол, сериализация, конфиги, checksum, файловые утилиты
- `src/server` — TCP-сервер и обработка control/upload соединений
- `src/client` — сканирование директории, sync flow, upload manager
- `tests` — unit-тесты для общего слоя
- `config/*.template` — шаблоны конфигов
- `scripts` — вспомогательные скрипты
- `docs` — вспомогательные материалы

## Зависимости
- `CMake >= 3.20`
- компилятор с поддержкой `C++20`
- `Boost` с `Boost.Asio` и `Boost.System`

Пример для Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake libboost-dev libboost-system-dev
```

## Сборка
```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Подготовка локальных конфигов
Создайте их один раз из шаблонов:

```bash
cp config/client.yaml.template config/client.yaml
cp config/server.yaml.template config/server.yaml
```

Потом отредактируйте:
- `config/client.yaml` под ваш `client_id`, путь к директории, IP и порт сервера
- `config/server.yaml` под путь хранения и параметры сервера

## Запуск сервера
```bash
mkdir -p demo/server_storage
./build/mydrive_server --config config/server.yaml
```

Для фонового запуска на Linux:

```bash
nohup ./build/mydrive_server --config config/server.yaml > server.log 2>&1 &
```

Проверка:

```bash
ss -ltnp | grep 9090
```

## Запуск клиента
```bash
mkdir -p demo/client_data
./build/mydrive_client --config config/client.yaml sync
```

Для генерации больших тестовых файлов:

```bash
./scripts/generate_large_files.sh ./demo/client_data
```

## Режим DMA
Поле `dma_enabled` в `config/client.yaml` переключает режим клиента:

- `dma_enabled: false` — обычная буферизованная передача
- `dma_enabled: true` — Linux `sendfile` путь

Для полноценной демонстрации DMA тесты лучше запускать на Linux-клиенте.

## Ограничения
- Сканируются только файлы верхнего уровня директории клиента
- Поддиректории намеренно не поддерживаются
- DMA-путь реализован только для Linux
