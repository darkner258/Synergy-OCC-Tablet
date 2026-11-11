# Synergy OCC Tablet – backlog

## Vydáno ve v1.3 (mění se jen exe, plugin zůstává z 1.2)
- [x] Obrazovka Jízda podle hry: linkospoj vpravo nahoře, datum/odchylka/čas o řádek výš a větší, bílý text v boxu odchylky, větší písmo zastávek, šedé časy mimo aktuální řádky, tučná světlejší cena, větší zóny a minuty

- [x] Menu (Jízda, Zařízení, Inf. systém, Nastavení): datum a čas vlevo nad záložkami Info a Nastavení, tučně a větší – jako ve hře

## Vydáno ve v1.2 (mění plugin)
- [x] Spořič podle skutečné kasy: velké segmentové hodiny, odchylka v barvě, datum, počet cestujících, příští zastávka
- [x] Počet cestujících na obrazovce Jízda (za panáčkem)
- [x] Obrazovka Činnost (přejezd, přistavení…): tabulka se vypisovala černě na černé, teď bíle
- [x] Manuální volba linkospoje: zvolené pole (Linka/Spoj/Cíl) žlutě, přesnější pozice polí, tmavý text jako ve hře (`occ_manual_select` – nová proměnná v pluginu)

## Další
- [ ] Nahlásit falešný poplach Microsoftu (microsoft.com/wdsi/filesubmission)
- [ ] „Vzít drobné“ přes trigger pluginu místo simulace klávesy – potřeba název akce Shift+T z OMSI (Možnosti → Klávesnice); jestli je to vestavěná akce OMSI a ne trigger skriptu, přes plugin to nepůjde
- [ ] Build přes MSVC místo mingw (méně falešných poplachů)
- [ ] Linuxová verze (OMSI přes Proton/Wine)
- [ ] Podpis přes SignPath (veřejný repozitář + GitHub Actions)

## Nápady (zatím ne)
- [ ] „Vzít drobné“: před stiskem přepnout na okno OMSI, upozornit na hru spuštěnou jako správce, ukázat cílové okno stisku. (Případ 7. 11. vyřešil restart PC.)
- [ ] Tablet neusne (Wake Lock), zvuk ťuknutí, uložené přihlášení (ID/PIN)
- [ ] Tržba směny, displej pro cestující na druhém zařízení, rozložení na výšku pro telefony

## Vydáno ve v1.1
- [x] Porty pluginu 38600/38601 (mimo dynamický rozsah Windows)
- [x] Výběr IP adresy v okně (všechny síťovky, nahoře Wi-Fi/LAN s bránou)
- [x] Tlačítko „Povolit ve firewallu“ + nápověda, když se tablet do 90 s nepřipojí
- [x] Obrázky kasy se hledají i v podsložkách Vehicles a v content folderu openOMSI; chybějící obrázek se ukáže v okně
- [x] Diagnostika v okně: neotevřený UDP port, openOMSI bez omsi-plugin-host32.exe, plugin nenačtený, plugin nainstalovaný dvakrát
- [x] Menu Jízda: Turnus, Linkospoj, Výchozí zastávka, stav odpočtu („Manipulace“)
- [x] Vzít lístek, Vzít drobné (dva řádky), panel Hotovost
- [x] Návod: antivirus (Defender, Avast), openOMSI, aktualizace z 1.0
