# FROSTMOURNE — uniwersalny loader, osobne paczki (work)

## Instalacja
Pobierz dwa artefakty z tego samego uruchomienia workflow gui-loader-x86 na work: Frostmourne_Universal_Loader_x86.zip i Frostmourne_Modules_x86.zip. Wypakuj oba do tego samego katalogu poza katalogiem WoW, zachowujac podkatalog modules/frostmourne-bootstrap. Loader nie zawiera Wow.exe. Uruchom FrostmourneGui.exe, wskaz zgodny klient 3.3.5a build 12340 x86, zaznacz modul w GUI i dopiero wtedy uruchom gre. Domyslnie eksperymentalny bootstrap jest wylaczony.

## Niezalezna aktualizacja
Zamknij gre przed podmiana DLL i manifestu. Aktualizuj caly folder jednego modulu, nie sama DLL. Loader x86 mozna zachowac przy aktualizacjach modulow implementujacych wspolny ABI 1.0. Nie mieszaj bibliotek i manifestow z roznych buildow. Mechanizm pobierania modulow z GitHuba oraz automatyczny rollback NIE sa jeszcze zaimplementowane w GUI.

## Kontrakt manifestu
W folderze modules/<id> plik module.json schema_version 1 zawiera id, version, file, sha256, client_sha256, architecture x86, abi_major 1, abi_minor 0, enabled_by_default false, dependencies (identyfikatory pozostalych modulow), assets (path, sha256 i install_path w Interface/AddOns) oraz options (key, type: bool/int/choice, default_value, min/max lub choices). Moduly z blednym hashem, architektura, ABI, fingerprintem klienta albo brakujaca lub cykliczna zaleznoscia sa blokowane. Ustawienia modulu: wybierz modul i kliknij Ustawienia modulu. GUI tworzy kontrolki z manifestu. Zapis w %LOCALAPPDATA%/Frostmourne/modules.cfg.

## Wspolny ABI
Nowe DLL x86 udostepniaja stdcall _Frostmourne_GetAbi@4 i _Frostmourne_Initialize@4, a dla deklarowanych opcji rowniez _Frostmourne_SetOption@4 (nazwy mozna okreslic w manifeście). GetAbi zwraca 0x00010000. Initialize przyjmuje 32 bajty zgodne z src/bootstrap/bootstrap.h i potwierdza PID, zwracajac 0xF1057A01. SetOption: DWORD size=212; DWORD target_pid; char key[64] UTF-8 NUL; char value[128] UTF-8 NUL; DWORD result, win32_error, observed_pid. Prawidlowe potwierdzenie zawiera result=0xF1057A01, error=0 i PID klienta. Wspolna inicjalizacja nie jest potwierdzeniem dzialania w rozgrywce.

Istniejacy FrostmourneBootstrap jest zachowany w trybie zgodnosci bootstrap_mode=legacy_bootstrap (eksperymentalne funkcje Auto Kick i Auto Pickpocket sa w nim polaczone). Ten tryb nie jest nowym ogolnym ABI.

## Granica weryfikacji
CI kompiluje Windows x86, weryfikuje PE32/I386, wybrane eksporty DLL, SHA256 i spojnosc dwoch niezaleznych ZIP-ow. CI nie uruchamia klienta WoW. Zaladowanie, inicjalizacja oraz skutecznosc kazdej funkcji w grze wymagaja osobnego testu. Pobieranie z GitHuba wprost przez loader, transakcyjny rollback oraz migracja opcji starych funkcji bootstrapu do generycznego ABI pozostaja niezrealizowane.
