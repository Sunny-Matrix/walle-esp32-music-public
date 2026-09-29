# WALI AI Learning Companion

([中文](README.md) | English | [日本語](README_ja.md))

WALI is an ESP32-S3 based AI learning companion for students, families, and teachers. It combines cloud language-model interaction with a display, microphone, speaker, touch input, addressable lights, servos, music playback, and mobile web tools.

## Features

- Voice interaction with wake, listening, thinking, speaking, playback, and exit states.
- Four-stage guided learning for mathematics, science, English, and Japanese.
- Learning summaries, difficulty analysis, and follow-up suggestions in a separate learning web page.
- Weather, time, and location information on the device home screen.
- Emotional support through voice, screen UI, eye lights, and short physical gestures.
- Music search, FFmpeg conversion, local caching, and device playback.
- Separate mobile pages for device control and learning records.
- ESP32-S3 firmware with display, audio, touch, lighting, and servo integration.

## Repository layout

```text
main/                         ESP32-S3 firmware
main/boards/robot-s3/         WALI board configuration
web-control/                  Mobile device-control page
web-learning/                 Learning records and feedback page
music-api-cos/                Music search, conversion, and audio service
docs/                         Protocol and development documentation
```

## Quick start

Use ESP-IDF 5.4 or newer:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM14 flash monitor
```

Start the two web pages locally:

```powershell
cd web-control
python -m http.server 8080

cd web-learning
python -m http.server 8081
```

For music service development:

```powershell
cd music-api-cos
npm install
npm start
```

Keep passwords, API keys, tokens, database exports, certificates, and local machine paths in local environment files. Do not commit them to the repository.

## Open-source notice

This repository contains WALI-specific product integration, board adaptation, interface work, guided-learning design, music service integration, and web tools built on an open-source ESP32 AI device software foundation. The upstream license and copyright notices remain in `LICENSE`; redistribution must comply with that file and the licenses of third-party dependencies.
