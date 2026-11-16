# Reprodukovatelný profil zařízení

Sběrač používá Python 3 (pouze standardní knihovnu), macOS systémové příkazy a
malý Objective-C++ pomocný program s Foundation, sestavený přes vybraný Xcode
pomocí `clang++ -std=c++23`. Neinstaluje nástroje a nemění napájení ani systémové
cache. Kompilace pomocného programu probíhá před vzorkováním; binárka se odstraní
s privátním dočasným adresářem. Nejde o runtime frameworku ani test podpory libc++.

Z kořene projektu:

```sh
python3 tools/machine-profile/collect.py \
  --output /tmp/instantee-device-profile \
  --storage-path "$PWD" --samples 5 --interval-seconds 1
python3 tools/machine-profile/verify.py /tmp/instantee-device-profile \
  --output /tmp/instantee-device-verification.json
python3 -m unittest discover -s tools/machine-profile -p 'test_*.py' -v
```

Pro každý sběr a ověření zvolit nový výstup. Existující soubory se nepřepisují.
`--storage-path` musí být existující adresář na budoucím výstupním svazku;
APFS se dotazuje přes skutečný mount point, nikoli libovolný podadresář.
Sběr má návratový kód 0 při shodě identity referenčního hardwaru, 2 při jiném
či neověřeném zařízení a 1 při selhání sběru. Kód 0 sám nepotvrzuje úplnost
profilu: povinně spustit i verifier. Ten vrací 0 pouze při úplném profilu,
shodě digestů a úspěšném porovnání s aktuálním strojem. Při nedostatečných
oprávněních vrátí neúspěch; nic neodhaduje z názvu modelu či marketingové RAM.

`raw/probes.json` ukládá argv, UTC začátek, délku dotazu v ns, návratový kód,
stderr a výstup každého příkazu. U `system_profiler` a `diskutil` jde o explicitní
výběr polí: sériová čísla, UUID, display EDID a uživatelská jména se neukládají.
Názvy disků `diskN` jsou identita tohoto bootu, nikoli trvalý identifikátor kusu.
Identitu hardwarové konfigurace doplňuje model média, kapacita v B, bus,
interní/externí umístění a APFS physical-store vazba. Profil nerozlišuje dva
fyzické kusy se zcela shodnou konfigurací.

Každá metrika nese `value`, `unit`, `source` a `reason`. `null` znamená
nedostupnost s důvodem; skutečná naměřená nula zůstává nulou. RAM má přímý
sysctl zdroj v B, kontrolovaný Foundation `physicalMemory`; nepřevádí se
řetězec „16 GB“ ze systémového přehledu. Swap se převádí z binárních K/M/G na B;
přesnost je omezena zaokrouhlením původního sysctl výpisu. Load average je
počet runnable jobs v oknech 1/5/15 minut. CPU procenta a systémová paměť
zůstávají v agregovaném `top` výstupu s jednotkami; druhý vzorek odpovídá
intervalu 1 s. `top` ani sběrač nejsou měření CPU procesu budoucího rendereru.

Termální enum a low power mode pocházejí z `NSProcessInfo` podle lokálního
`Foundation.framework/Headers/NSProcessInfo.h`. Neznámý budoucí enum je `null`.
`pmset -g therm` je jen doplňkový diagnostický údaj: absence historického
varování nedokazuje `nominal`. Čas vzorků používá UTC a monotónní offsety v ns;
skutečné intervaly jsou uložené, dotazy jednotlivých metrik nejsou atomické.
Krátká řada popisuje stav při vývoji, nikoli thermal/endurance test.

Xcode se hledá v `/Applications/Xcode*.app`, v aktivním developer adresáři,
v případném `DEVELOPER_DIR` a v `/Library/Developer/CommandLineTools`.
SDK jsou inventarizována z jejich skutečných `SDKSettings.json`, aliasy se
deduplikují přes resolved path. Verze aktivního SDK i jeho build jsou navíc
z `xcrun`. Standalone CLT nemají vlastní Xcode verzi: tato hodnota je `null`
s důvodem, přítomný je jejich package receipt a SDK. Libovolné archivované
instalace mimo uvedené cesty nejsou prohledávány. Verze se zde nezamykají.

Testy používají uloženou měřenou evidenci jako regresní vstup a navíc ověřují
chybějící metriky, odmítnuté dotazy, timeouty, převody jednotek, odlišné GPU
varianty a ochranu existující evidence. Nespouštějí systémové měření.
