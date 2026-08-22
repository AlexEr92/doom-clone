# Unity — вендоренный фреймворк юнит-тестов

Версия: **v2.7.0**
Источник: https://github.com/ThrowTheSwitch/Unity
Лицензия: MIT, полный текст — в `LICENSE.txt` рядом.

Файлы лежат прямо в репозитории, без git submodule: сборка проекта должна
работать сразу после клонирования, без `git submodule update --init`. Так же
подключён `vendor/stb_image.h`.

Из апстрима взяты только три файла ядра — `src/unity.c`, `src/unity.h`,
`src/unity_internals.h`; генераторы раннеров на Ruby, документация и
собственные тесты Unity не нужны, тестовые исполняемые файлы в `tests/`
вызывают `RUN_TEST()` из своих `main()` вручную.

## Как обновить

Скачать архив нужного тега, скопировать те же три файла и `LICENSE.txt`,
поправить номер версии в этом файле:

```bash
curl -sL https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.7.0.tar.gz | tar xz
cp Unity-2.7.0/src/unity.c Unity-2.7.0/src/unity.h \
   Unity-2.7.0/src/unity_internals.h Unity-2.7.0/LICENSE.txt vendor/unity/
```

Локальных правок в исходниках нет — файлы совпадают с апстримом побайтово.
