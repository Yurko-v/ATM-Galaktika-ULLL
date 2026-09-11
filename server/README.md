# Сервер сквоков Galaxy ATM System

Один сервер на всех: хранит, какие коды выданы каким бортам, выдаёт свободный код по
запросу плагина и освобождает его, только когда пилот ушёл из сети VATSIM.

## Что внутри

| Путь | Что это |
|---|---|
| `schema.sql` | Таблицы MySQL (импортируется один раз) |
| `config.sample.php` | Пример настроек — на сервере копируется в `config.php` (не в git) |
| `lib/` | Общий код (БД, выбор кода) |
| `cron/vatsim_sync.php` | Задача раз в минуту: данные VATSIM, освобождение кодов |
| `public/api/assign.php` | `POST` — выдать код борту |
| `public/api/report.php` | `POST` — код, введённый диспетчером вручную |
| `public/api/state.php` | `GET` — все занятые коды |
| `public/api/health.php` | `GET` — проверка, что всё живо (без ключа) |

Наружу смотрит только `public/`; `lib`, `cron` и `config.php` из интернета недоступны.

## Установка на Beget (один раз)

Сайт, который уже есть на аккаунте, не затрагивается: сервис ставится отдельным сайтом
на поддомен, а файлы берутся прямо из репозитория на GitHub.

### 1. Сайт, поддомен, SSL, PHP

- Панель → «Сайты» → создать сайт `squawk` (папка `~/squawk/public_html`).
- «Домены и поддомены» → поддомен `squawk.<домен>` → привязать к сайту `squawk`.
- «SSL сертификаты» → бесплатный сертификат для `squawk.<домен>`.
- В настройках сайта выбрать PHP 8.x.

### 2. База

Панель → «MySQL» → создать базу (на Beget пользователь = имя базы, хост `localhost`).
phpMyAdmin → «Импорт» → `server/schema.sql`.

### 3. SSH

Главная страница панели → включить SSH. Адрес там же; логин и пароль — от панели.

### 4. Ключ, чтобы Beget мог читать приватный репозиторий

На Beget по SSH:
```bash
ssh-keygen -t ed25519 -f ~/.ssh/github_deploy -N ""
printf "Host github.com\n  IdentityFile ~/.ssh/github_deploy\n" >> ~/.ssh/config
cat ~/.ssh/github_deploy.pub
```
Строку из `cat` добавить на GitHub: репозиторий → Settings → Deploy keys → Add deploy key
(без галочки «Allow write access»).

### 5. Клон и папка сайта

```bash
git clone git@github.com:Yurko-v/ATM-Galaktika-ULLL.git ~/squawk/repo
rm -rf ~/squawk/public_html && ln -s ~/squawk/repo/server/public ~/squawk/public_html
```

### 6. config.php

```bash
cp ~/squawk/repo/server/config.sample.php ~/squawk/repo/server/config.php
openssl rand -hex 32
nano ~/squawk/repo/server/config.php
```
Вписать базу из шага 2, в `api_key` — строку из `openssl`. Тот же ключ потом идёт в
`GalaxyATMSystem.json` каждого диспетчера (`Squawk.ApiKey`). `config.php` в `.gitignore`,
`git pull` его не трогает.

### 7. Cron

Узнать домашнюю папку: `echo $HOME`. Панель → «CronTab» → каждую минуту:
```
/usr/local/php-cgi/8.2/bin/php ДОМАШНЯЯ_ПАПКА/squawk/repo/server/cron/vatsim_sync.php >> ДОМАШНЯЯ_ПАПКА/squawk/cron.log 2>&1
```
`8.2` — версия PHP сайта.

### 8. Проверка

- `https://squawk.<домен>/api/health.php` → `"ok": true`; через минуту после cron —
  `"network_fresh": true`. В `~/squawk/cron.log` каждую минуту строка
  `pilots=... held=... released=...`.
- Выдать код:
  ```
  curl -X POST https://squawk.<домен>/api/assign.php -H "X-Api-Key: КЛЮЧ" -H "Content-Type: application/json" -d "{\"callsign\":\"TEST1\",\"position\":\"ULLI_DEL\"}"
  ```
  Повтор с другой `position` → тот же код (`"existing": true`); `"new": true` → другой.
- `curl https://squawk.<домен>/api/state.php -H "X-Api-Key: КЛЮЧ"` → все выданные коды.
  `TEST1` не в сети, так что через 10 минут cron его освободит.

## Автодеплой с GitHub

`.github/workflows/deploy-server.yml`: при push в `main`, если менялось что-то в
`server/`, GitHub заходит на Beget по SSH и делает `git pull`. Запустить вручную:
GitHub → Actions → «Deploy squawk server» → «Run workflow».

Настройка (один раз):

1. Ключ для GitHub Actions — на своём компьютере или на Beget:
   ```bash
   ssh-keygen -t ed25519 -f beget_actions -N "" -C github-actions
   ```
2. Публичную часть — на Beget в `~/.ssh/authorized_keys`:
   ```bash
   cat beget_actions.pub >> ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys
   ```
3. GitHub → репозиторий → Settings → Secrets and variables → Actions:
   - **Secrets:** `BEGET_HOST` (адрес SSH из панели), `BEGET_USER` (логин Beget),
     `BEGET_SSH_KEY` (всё содержимое файла `beget_actions`, приватного).
   - **Variables** (необязательно): `BEGET_REPO_DIR`, если клон не в `squawk/repo`;
     `SQUAWK_HEALTH_URL` = `https://squawk.<домен>/api/health.php` — после деплоя
     workflow проверит, что сервер отвечает.
4. Файл `beget_actions` после этого удалить.

Изменения `schema.sql` автодеплой не применяет — новую структуру базы накатывать
вручную через phpMyAdmin.

Если на Beget по SSH `git` не находится, команды выполняются в окружении с
программами: `ssh localhost -p222`.

## Как выбирается код

1. Диапазоны позиции из `config.php` (по `.ese`): ULLI_GND — 0720–0757,
   ULLL_R/R5/R6 — 0701–0717, остальные ULLI/ULLL/ULOL — 0760–0777. Когда свои
   кончились — общий пул 0701–0777.
2. Пропускаются: коды, выданные другим бортам; коды, которые пищит или которые
   назначены любому пилоту VATSIM в радиусе 1000 км от ULLI (соседние РПИ тоже);
   0000, 1000, 1200, 2000, 7000, 7500, 7600, 7700.
3. Из свободных первым идёт код, который ни разу не выдавался, затем тот, что
   освободился раньше всех — только что освобождённый код уходит последним.

Код держится за бортом, пока пилот в сети. Когда пилота нет в данных VATSIM дольше
`release_after_min` (10 минут), код возвращается в пул. Если данные VATSIM не приходят
(сбой сети или cron), ничего не освобождается.
