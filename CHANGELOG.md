# Changelog

Todas as mudanças notáveis neste projeto estão documentadas neste ficheiro.

## [0.4.0] — Em desenvolvimento

### Aviso
- Este release ainda não foi testado em hardware real.
- Parte das alterações foram desenvolvidas com assistência de IA.

### mDNS
- Registado `paperwake.local` no WiFi
- Reinicia mDNS ao trocar de rede WiFi
- Para mDNS ao entrar em suspensão

### Modo Bateria
- Nova flag de build: `-DENABLE_BATTERY`
- Novo ambiente PlatformIO: `battery`
- Nova setting na tab System: "Battery" (On/Off)
- Deteção de power-loss: transição mains→battery define flag
- E-paper mostra aviso "Power loss" uma única vez

### RTC DS3231
- Nova flag de build: `-DENABLE_RTC`
- Novos ambientes PlatformIO: `rtc` e `battery-rtc`
- Nova setting na tab System: "RTC" (On/Off)
- DS3231 via I2C (GPIO 21=SDA, GPIO 3=SCL, endereço 0x68)
- NTP sincroniza RTC quando WiFi disponível
- RTC usado como fallback quando WiFi/NTP indisponível

### Compilação
- Todos os 6 ambientes PlatformIO compilam com sucesso

### Fontes com acentos portugueses
- Regeneradas todas as 14 fontes com range Latin-1 (0x20–0xFF) via `tools/regenerate_fonts.py`
- Acentos portugueses agora renderizam corretamente no e-paper (á é ç Ã ã etc.)
- Strings PT atualizadas: "Ajustes", "Contínuo", "Estático", "Ligação", "Página", "Português", "Álbum", etc.
- Web portal mantém UTF-8 (não afetado)
- ~100KB adicionais de flash nas fontes (vs 8.8MB libertados na Fase 3.5)

### Multi-alarmes
- Até 8 alarmes com dias da semana (bitmask Dom–Sáb)
- Alarmes one-shot (daysMask=0) auto-desativam-se após disparar
- Setting "Ring mode": Continuous ou Auto-off
- Migração automática do formato NVS antigo
- Relógio, countdown e rampa de luz usam o próximo alarme futuro

### Portal web admin
- Dashboard, CRUD de alarmes, settings, OTA, backup/restauro JSON
- Página WiFi (mudar de rede com fallback automático)
- OTA (atualização de firmware via browser)

### i18n Português
- Setting "Language" (English/Português), persistida em NVS
- Todas as strings do e-paper traduzidas

### Sons no SD
- BuiltinSounds.h removido do build (~8.8MB flash libertados)
- 4 sons ambientes agora em /sounds/_builtin_*.pcm no microSD
- FallbackTone.h mantido (~600KB, sempre disponível)

### Bateria + Deep Sleep (env `battery`)
- Módulo Power.h/.cpp (atrás de -DENABLE_BATTERY)
- Reading ADC do divisor de tensão (GPIO 38, ADC1_CH0)
- Deep sleep com wakeup por timer + botões (GPIO 1, 2, 15)
- Setting "Sleep refresh" (Static/30min/1h/Off)
- Ícone de bateria no clock face
- Setting "Battery" (On/Off) — utilizador indica se hardware de bateria está ligado
- Deteção de power-loss (aviso no e-paper)

## [0.3.x] — Histórico anterior
- Relógio com NTP + fallback AP
- Single alarm com edição por botões
- Som via SD (upload portal)
- Wake-up light + night light + frontlight
- Subtitles de despertar em inglês apenas
- Fontes ASCII 0x20–0x5A (sem acentos)
