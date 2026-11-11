# Synergy OCC Tablet – kontext pro Claude Code

Ovládání kasy **Synergy OCC** (mod do OMSI 2, autoři P3ta, Jack Black, Swix) z tabletu přes prohlížeč.
Autor nástroje: Arteyo258 (GitHub `darkner258`). Vydáno se svolením P3ty – i zdrojáky na GitHubu.
Komunita: Discord, distribuce přes GitHub Releases (dřív Google Drive).

## Jak se mnou pracovat
- Komunikace **česky**, stručně, jedno doporučení, celé soubory/příkazy připravené ke zkopírování.
- Upozorni na skutečné nevýhody řešení, nepřikyvuj.
- **Novou verzi (exe, zip, tag) stav jen když řeknu.** Úpravy dělej ve zdrojácích, ověř, a čekej.
- Verzování: poslední vydaná je **1.3**. Změny sbírej v `BACKLOG.md` a vydávají se dávkově.
  Plugin se mění jen když je to nutné (uživatelé pak musí plugin přeinstalovat a restartovat OMSI) – v changelogu to vždy napiš.
- **Changelog vždy formátovat pro Discord** (`**tučně**`, emoji, krátké odrážky, netechnicky), v code blocku ke zkopírování.
- Při ladění grafiky posílám screeny ze hry („srovnej podle reálu“) – měř pozice v pixelech textury a porovnávej hra / současný stav / oprava.

## Architektura
```
OMSI 2 ─ plugin/SynergyBridge.dll ─UDP─ app/SynergyTablet.exe ─HTTP :8765─ tablet (app/index.html)
          stav JSON ~100 ms → 127.0.0.1:38600
          triggery (jméno) ← 127.0.0.1:38601
```
- **plugin/** – 32bit OMSI plugin (stdcall exporty `PluginStart`, `PluginFinalize`, `AccessVariable`,
  `AccessStringVariable`, `AccessSystemVariable`, `AccessTrigger`, link `-Wl,--kill-at`).
  - `.opl` obsahuje `[varlist]`, `[stringvarlist]`, `[triggers]`; generuje ho `plugin/gen.py` spolu s `names.h`
    (seznamy VARS / STR / T). Po změně seznamů spusť `python3 plugin/gen.py`.
  - Trigger: drží se 3 snímky, pak se pustí; pro `ticketprinter_button_*` a `cashdesk_changer_*` se po něm pošle i `*_off`.
  - OMSI čte řetězce skriptů jako Latin-1 → plugin převádí U+0080–U+00FF přes CP1250 (jinak „Kè“ místo „Kč“).
- **app/** – Win32 aplikace bez závislostí (mingw-w64, statický build). V exe jsou jako RCDATA `index.html`,
  `SynergyBridge.dll` a `.opl` (`app/src/app.rc`).
  - HTTP: `/` UI, `/state` poslední stav, `POST /press {t}` trigger, `/tex?p=` textury Synergy z disku uživatele.
  - `key_shift_t` = Shift+T přes SendInput (scancody 0x2A, 0x14) do okna v popředí („Vzít drobné“).
  - Najde OMSI přes registry Steamu + `libraryfolders.vdf`; textury hledá ve `Vehicles\SynergyOCC`, `Vehicles\*\SynergyOCC`
    a v content složce openOMSI.
  - Instalace pluginu do `OMSI 2\plugins`; při `ERROR_ACCESS_DENIED` se spustí znovu s `runas` a `--install "<dir>"`.
  - Výběr IP (`GetAdaptersAddresses`), tlačítko „Povolit ve firewallu“ (netsh, elevated), QR kód (nayuki qrcodegen, MIT).
  - Diagnostika openOMSI z `%USERPROFILE%\.openomsi\game.log`. Konfigurace `%APPDATA%\SynergyTablet\config.ini` (`omsi_dir`, `ip`, `port`).
  - Verze: `APP_VERSION` v `main.cpp` + VERSIONINFO v `app.rc` (při vydání změnit obojí).
- Repo **neobsahuje žádné soubory Synergy** (textury, skripty) – a tak to musí zůstat.

## Webové UI (app/index.html)
- Souřadnice jsou v pixelech textury obrazovky **1000 × 620**; celé se škáluje (`fit()`), pod tím lišta fyzických tlačítek.
- Pozadí = textura z `menu_texture` (`..\..\SynergyOCC\texture\...`).
- `HOT` = hotspoty `[trigger, [x,y,w,h], podmínka]`, `TXT` = texty `[proměnná, rect, px, zarovnání, podmínka, třídy]`,
  `BEZEL` = spodní lišta (Vpřed, Vzad, F1, F4, F5, Menu, ▲▲, ▼▼, ▲, ▼, Vzít lístek, Vzít drobné, Hotovost).
- Třídy: `w` bílá, `b` tučně, `g` šedá (#5a5a5a), `pr` cena (#ff6f66), `nv` černá.
- Zvýraznění vybraného řádku/volby = výřez z „_2“ textur (`occ_main2.png`, `occ_manual_2.png`) jako CSS background.
- Seznamy: řetězce s `@` mezi řádky, 17 řádků, `ROW(k) = [2, 30+34.8k, 716, 34.8]`.
- `?debug=1` ukáže hotspoty.

### Obrazovky podle `navigator_menu_visible`
| hodnota | obrazovka |
|---|---|
| 0 | Jízda (hlavní) |
| 1 / 32 / 51 / 54 | menu Jízda / Zařízení / Inf. systém / Nastavení |
| 5 | spořič (segmentové hodiny, SVG `segClock()`) |
| 34 | ruční linkospoj (zvýraznění dle `occ_manual_select` 0/1/2) |
| 57 | činnost (tabulka, bílý text) |
| 30 / 33 / 35 | seznam turnusů, 31 seznam linkospojů, 52 / 53 služební cíle |
| 9 / 10 | platba kartou (EMV / EP), 11 / 98 tisk |
| 4 / 3 / 96 | přihlášení, 55 / 56 GPS hlášení |

### Užitečné proměnné
- `IBIS_odpocet` → barva odchylky: `floor(v/2)`: 0 brzo (červená), 1 OK (zelená), 2 mírné zpoždění (žlutá), 3 zpoždění (oranžová), 4 bílá.
- `GivenTicket` přejde z ≥0 na -1 = cestující si vzal lístek (nuluje počítadlo hotovosti).
- `pocet_cestujicich_str`, `occ_linkospoj_row0..16` (červený pruh u linkospojů), `occ_print_text`, `stop0..6_x`.

### Hotovost (Kč → trigger)
1 → `cashdesk_changer_0_05`, 2 → `0_10`, 5 → `0_50`, 10 → `1_00`, 20 → `2_00`, 50 → `5_00`, 100 → `8_00`. Bankovka 200 Kč v kase není.

## Sestavení a testování
- `./build.sh` (Linux/WSL, `mingw-w64`) → `dist/SynergyTablet.exe`. CI: `.github/workflows/build.yml` (Ubuntu 24.04).
- Release: založit tag `v1.x` (nebo release v GitHub UI) → CI nahraje `SynergyTablet_v1.x.zip`.
- Podpis: SignPath Foundation – krok ve workflow se zapne po nastavení `SIGNPATH_API_TOKEN` (secret) a `SIGNPATH_ORGANIZATION_ID` (variable).
  Podepisuje se nejdřív plugin (artifact configuration `plugin`), ten se vloží do exe a pak se podepíše exe (`app`).
  Konfigurace pro SignPath jsou v `docs/signpath/`. Podepsaný plugin má jiné bajty → aplikace u uživatelů hlásí starý plugin
  (porovnává bajty), takže první podepsaná verze = přeinstalovat plugin + restart OMSI.
- Bez OMSI: spusť exe a `python tools/sim.py main|turnus|menu|cinnost|sporic|manual` – posílá falešný stav na UDP 38600.

## Známé problémy u uživatelů
- **Antivirus** (Defender, Avast) hlásí nepodepsané exe → falešný poplach; postup v `docs/CTI_MNE.txt`. Řešením je podpis (SignPath).
- **openOMSI** (≥ 0.3.45): 32bit pluginy běží v `omsi-plugin-host32.exe`, který musí ležet vedle hry; když chybí (často smazaný antivirem),
  žádný plugin se nenačte. Plugin patří do `OMSI 2\plugins`, ne do pluginů openOMSI.
- Tablet se nepřipojí: firewall, izolace klientů na AP, jiná podsíť, VPN adaptér (zvolit jinou IP v aplikaci).
- Po aktualizaci pluginu vždy restartovat OMSI. Divné záseky („6 FPS“, nefunkční Vzít drobné) jednou vyřešil restart PC/hry.
