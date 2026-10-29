# Jade Path — PSP smoke test

Это проверочная сборка PSP-порта. Нужно убедиться, что GitHub Actions
собирает EBOOT.PBP, прежде чем переносить в C всю игру.

## Загрузка через браузер

1. Открой репозиторий.
2. Add file → Upload files.
3. Загрузи `main.c`, `Makefile`, `README-PSP.txt`.
4. Commit changes.
5. Add file → Create new file.
6. Имя файла: `.github/workflows/build-psp.yml`.
7. Вставь содержимое `build-psp.yml` из архива.
8. Commit changes.
9. Actions → Build PSP EBOOT → дождись зелёной галочки.
10. Внизу страницы запуска: Artifacts → JadePath-PSP.

Внутри архива артефакта будет `JadePath/EBOOT.PBP`.
Скопируй папку `JadePath` в `ms0:/PSP/GAME/` на карте памяти PSP.

Требуется CFW 6.60/6.61 PRO/LME/ARK.

Экран проверки: поле 7×7, золотой курсор. D-pad — курсор, Start — выход.
