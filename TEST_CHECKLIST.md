# MyDrive Test Checklist

Этот файл нужен как пошаговый сценарий тестирования перед защитой и как список артефактов для отчета.

Основная схема стенда:
- Linux-клиент: запускает `mydrive_client`, `tcpdump`, `ss`
- Linux-сервер `103.27.157.12`: запускает `mydrive_server`, хранит файлы, показывает `ss`
- Mac: открывает `.pcap` в Wireshark и делает скриншоты

## 0. Подготовка

### Клиент
```bash
cd ~/networks_hw4
git pull
cmake --build build -j
mkdir -p demo/client_data
```

Проверь [config/client.yaml](/Users/mnntn/Documents/networks_course/hw_4/config/client.yaml):
```yaml
client_id: smoke-001
directory: ./demo/client_data
server_host: 103.27.157.12
server_port: 9090
max_connections: 4
dma_enabled: false
```

### Сервер
```bash
cd ~/networks_hw4
git pull
cmake --build build -j
pkill mydrive_server
nohup ./build/mydrive_server --config config/server.yaml > server.log 2>&1 &
```

Проверь, что сервер слушает порт:
```bash
ss -ltnp | grep 9090
```

### Скриншоты
- Сервер: `ss -ltnp | grep 9090`
- Сервер: активный процесс `mydrive_server`
- Клиент: актуальный `config/client.yaml`

## 1. Test 1: первая синхронизация маленьких файлов

### Клиент
```bash
find demo/client_data -type f -delete
printf 'alpha\n' > demo/client_data/a.txt
printf 'beta\n' > demo/client_data/b.txt
printf 'gamma\n' > demo/client_data/c.txt
ls -lh demo/client_data
time ./build/mydrive_client --config config/client.yaml sync
```

### Сервер
```bash
find demo/server_storage -maxdepth 2 -type f
ls -lh demo/server_storage/smoke-001
```

### Что должно получиться
- Клиент отправляет `a.txt`, `b.txt`, `c.txt`
- На сервере появляются эти три файла в `demo/server_storage/smoke-001/`

### Скриншоты
- Клиент: вывод `ls -lh demo/client_data`
- Клиент: успешный `sync` с upload-логами
- Сервер: `find demo/server_storage -maxdepth 2 -type f`
- Сервер: `ls -lh demo/server_storage/smoke-001`

## 2. Test 2: повторная синхронизация без изменений

### Клиент
```bash
time ./build/mydrive_client --config config/client.yaml sync
```

### Что должно получиться
- В выводе только `Scanning ...`
- Upload-ов быть не должно

### Скриншоты
- Клиент: no-op sync без upload-логов

## 3. Test 3: инкрементальная синхронизация

### Клиент
```bash
printf 'alpha\nchanged\n' > demo/client_data/a.txt
printf 'delta\n' > demo/client_data/d.txt
ls -lh demo/client_data
time ./build/mydrive_client --config config/client.yaml sync
```

### Сервер
```bash
ls -lh demo/server_storage/smoke-001
wc -c demo/server_storage/smoke-001/a.txt demo/server_storage/smoke-001/b.txt demo/server_storage/smoke-001/c.txt demo/server_storage/smoke-001/d.txt
cksum demo/server_storage/smoke-001/a.txt demo/server_storage/smoke-001/d.txt
```

### Что должно получиться
- Клиент отправляет только `a.txt` и `d.txt`
- На сервере обновляется `a.txt`
- На сервере появляется `d.txt`

### Скриншоты
- Клиент: изменение файлов (`ls -lh demo/client_data`)
- Клиент: sync, где видны только `upload a.txt` и `upload d.txt`
- Сервер: `ls -lh demo/server_storage/smoke-001`
- Сервер: `wc -c` и `cksum` для `a.txt` и `d.txt`

## 4. Test 4: параллельная передача больших файлов без DMA

### Клиент
Подготовка файлов:
```bash
find demo/client_data -type f -delete
dd if=/dev/urandom of=demo/client_data/p1.bin bs=1M count=100 status=progress
dd if=/dev/urandom of=demo/client_data/p2.bin bs=1M count=100 status=progress
dd if=/dev/urandom of=demo/client_data/p3.bin bs=1M count=100 status=progress
dd if=/dev/urandom of=demo/client_data/p4.bin bs=1M count=100 status=progress
ls -lh demo/client_data
```

Измени `config/client.yaml`:
```yaml
client_id: parallel-off-001
directory: ./demo/client_data
server_host: 103.27.157.12
server_port: 9090
max_connections: 4
dma_enabled: false
```

Терминал 1:
```bash
sudo tcpdump -i any -w parallel_off.pcap host 103.27.157.12 and tcp port 9090
```

Терминал 2:
```bash
watch -n 1 "ss -tnp | grep 103.27.157.12:9090"
```

Терминал 3:
```bash
time ./build/mydrive_client --config config/client.yaml sync
```

После завершения останови `tcpdump` и `watch` через `Ctrl+C`.

### Сервер
Во время передачи:
```bash
watch -n 1 "ss -tnp | grep 9090"
```

После передачи:
```bash
find demo/server_storage -maxdepth 2 -type f | grep parallel-off-001
```

### Что должно получиться
- Клиент отправляет 4 файла
- Передача идёт параллельно
- `ss` показывает несколько одновременных TCP-соединений
- Получается файл `parallel_off.pcap`

### Скриншоты
- Клиент: `ls -lh demo/client_data` с 4 большими файлами
- Клиент: вывод sync с `upload p1.bin ... p4.bin`
- Клиент: `time ... sync`
- Клиент: `ss -tnp` во время передачи, где видно несколько соединений
- Сервер: `ss -tnp | grep 9090` во время передачи
- Сервер: `find ... | grep parallel-off-001`

## 5. Test 5: параллельная передача больших файлов с DMA

### Клиент
Не меняя набор файлов, измени `config/client.yaml`:
```yaml
client_id: parallel-on-001
directory: ./demo/client_data
server_host: 103.27.157.12
server_port: 9090
max_connections: 4
dma_enabled: true
```

Терминал 1:
```bash
sudo tcpdump -i any -w parallel_on.pcap host 103.27.157.12 and tcp port 9090
```

Терминал 2:
```bash
watch -n 1 "ss -tnp | grep 103.27.157.12:9090"
```

Терминал 3:
```bash
time ./build/mydrive_client --config config/client.yaml sync
```

### Сервер
Во время передачи:
```bash
watch -n 1 "ss -tnp | grep 9090"
```

После передачи:
```bash
find demo/server_storage -maxdepth 2 -type f | grep parallel-on-001
```

### Что должно получиться
- Sync проходит успешно с `dma_enabled: true`
- Есть несколько параллельных соединений
- Получается файл `parallel_on.pcap`

### Скриншоты
- Клиент: `config/client.yaml` с `dma_enabled: true`
- Клиент: вывод sync и `time`
- Клиент: `ss -tnp` во время передачи
- Сервер: `ss -tnp | grep 9090`
- Сервер: `find ... | grep parallel-on-001`

## 6. Test 6: замеры 250 / 500 / 1000 MB

Для каждого размера нужен отдельный прогон с `DMA off` и `DMA on`.

### 250 MB
```bash
find demo/client_data -type f -delete
dd if=/dev/urandom of=demo/client_data/file_250.bin bs=1M count=250 status=progress
```

Конфиг 1:
```yaml
client_id: dma-off-250
max_connections: 1
dma_enabled: false
```

```bash
time ./build/mydrive_client --config config/client.yaml sync
```

Конфиг 2:
```yaml
client_id: dma-on-250
max_connections: 1
dma_enabled: true
```

```bash
time ./build/mydrive_client --config config/client.yaml sync
```

### 500 MB
```bash
find demo/client_data -type f -delete
dd if=/dev/urandom of=demo/client_data/file_500.bin bs=1M count=500 status=progress
```

Потом:
- `client_id: dma-off-500`, `dma_enabled: false`
- `client_id: dma-on-500`, `dma_enabled: true`

### 1000 MB
```bash
find demo/client_data -type f -delete
dd if=/dev/urandom of=demo/client_data/file_1000.bin bs=1M count=1000 status=progress
```

Потом:
- `client_id: dma-off-1000`, `dma_enabled: false`
- `client_id: dma-on-1000`, `dma_enabled: true`

### Что записывать
- Размер файла
- `client_id`
- `max_connections`
- `dma_enabled`
- `real` из `time`
- Клиент: `time` для каждого нужного прогона
- Клиент: при желании `ls -lh demo/client_data` для каждого размера
