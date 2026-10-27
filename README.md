# FileCollector 
Потокобезопасный класс на C++17 для сборки файлов из чанков, приходящих в рандомном порядке, с возможными пересечениями и повторениями.

## Интерфейс

```cpp
class FileCollector {
public:
    // Создаёт/сбрасывает состояние файла. fileSize == 0 => файл сразу собран. При повторном вызове сбрасывает состояние файла
    void CollectFile(uint32_t fileId, size_t fileSize);

    // Добавляет чанк. pos - позиция в файле. Чанк за границей обрезается
    // Игнорируется, если файл не создан, уже собран, или pos >= размера файла
    void OnNewChunk(uint32_t fileId, size_t pos, std::vector<uint8_t>&& chunk);

    // возвращает собранный файл (чтобы избежать копирования используется shared_ptr) или nullptr, если не готов.
    std::shared_ptr<const std::vector<uint8_t>> GetFile(uint32_t fileId) const;

    // Возвращает копию диапазона [offset,offset+size) или nullopt, если
    // диапазон выходит за границы или не все байты получены.
    // не требует завершенности файла! Только покрытие диапазона
    std::optional<std::vector<uint8_t>> GetPartFile(
        uint32_t fileId, size_t offset, size_t size) const;
};
```

### Модель синхронизации
Глобальный **shared_mutex** защищает только поиск в **unordered_map<fileId, FileState>** (**shared** на чтение, **unique** на вставку). Файловый **shared_mutex** защищает содержимое FileState (буфер, маску, счётчик) (**unique** на инициализацию и заполнение, **shared** на чтение файла). Глобальный отпускается до взятия per-file — разные файлы не блокируют друг друга.

## Сборка и тесты

Требуется CMake 3.16+, компилятор с C++17. Google Test подтягивается через FetchContent.

```bash
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
Sanitizers (взаимоисключающие):

```bash
# ASan + UBSan
cmake -B build-asan -DFILECOLLECTOR_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# TSan
cmake -B build-tsan -DFILECOLLECTOR_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

санитайзеры протестированы на gcc (linux fedora);

## Тестирование

34 теста: базовые сценарии (пустой файл, неизвестный id, чанки вразнобой, пересечения, дубликаты), границы (чанк за концом, pos = SIZE_MAX, пустой чанк), многопоточные стресс-тесты (ConcurrentLastByteWrite, GetFileStormDuringCompletion, ChaosCollectChunkRead — одновременные OnNewChunk/CollectFile/GetFile и парочку тестов на GetPartFile).

Все тесты проходят под TSan (гонок нет) и ASan+UBSan (утечек и UB нет).


## Возможные улучшения

- Память, заменить вектор флагов заполненности файла на битсет. До 8х меньше памяти.
- Готовый интерфейс для записи в поток WriteTo(ostream&);

### Работу выполнил Леонов Степан