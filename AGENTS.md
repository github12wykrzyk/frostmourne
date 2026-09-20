# FROSTMOURNE — INSTRUKCJE PROJEKTU

## CEL NADRZĘDNY
Repozytorium: https://github.com/github12wykrzyk/frostmourne
Klient: World of Warcraft 3.3.5a, build 12340, Windows x86.

**Priorytet: maksymalnie skrócić czas od opisu funkcji do gotowej, skompilowanej, zweryfikowanej DLL i kompletnego ZIP-a do testu w grze.** Analizy, infrastruktura i testy mają służyć dostarczeniu rzeczywistego działania, a nie zastępować implementację. Szybkość nie usprawiedliwia pomijania kompatybilności i stabilności ani deklarowania niepotwierdzonych rezultatów.

## MODEL PRACY
Projekt prowadzi przede wszystkim AI: samodzielnie obsługuje GitHub, kod, integrację, kompilację Windows x86, testy, manifesty, loader, updater i paczki. Użytkownik opisuje funkcję, testuje gotowe buildy, zgłasza błędy i akceptuje wersje stabilne. Nie zlecaj mu ręcznego zarządzania repozytorium ani kompletowania plików, jeśli możesz zrobić to sam. Nie deklaruj operacji, których nie wykonano.

## START KAŻDEGO ZADANIA
Źródłem prawdy jest aktualny GitHub. Czytaj w kolejności:
1. AGENTS.md
2. AI_START_HERE.md
3. AI_INDEX.json
4. CURRENT.json
5. runtime/current.json
6. Wyłącznie pliki potrzebnego modułu, jego testów i procesu budowania.

Brakujące pliki startowe utwórz podczas inicjalizacji. Nie odtwarzaj stanu z pamięci rozmów ani dawnych ZIP-ów, gdy dane są w repozytorium. Nie skanuj bez potrzeby całego repozytorium i archiwów.

## NAJSZYBSZA DROGA DO DZIAŁAJĄCEJ DLL
Dla żądania funkcji w grze oczekiwany wynik to **kompletna implementacja, DLL i gotowa paczka testowa**, nie sam kod, prompt, dokumentacja, osobny EXE testowy ani silnik decyzyjny bez połączenia z klientem.

Przebieg: określ zachowanie w grze → znajdź istniejący moduł i loader → potwierdź kompatybilność klienta → zaimplementuj najmniejszą kompletną funkcję wraz z rzeczywistym adapterem klienta → zbuduj DLL x86 → zweryfikuj → przygotuj i udostępnij ZIP → popraw według wyników testu.

Po pierwszej spójnej implementacji od razu kompiluj. Rozbudowuj istniejącą DLL zamiast tworzyć osobną dla drobnej funkcji. Nowe opcje regulowane przez użytkownika dodawaj do GUI z automatycznym zapisem i odczytem ustawień. Nie rozwijaj pobocznej infrastruktury kosztem dostarczenia działającej funkcji.

Rozróżniaj statusy:
- CORE_ONLY — działa tylko logika, brak akcji w grze;
- DLL_BUILT — DLL skompilowana i zweryfikowana statycznie;
- IN_PROCESS_TESTED — DLL załadowana i zainicjalizowana w kliencie;
- GAMEPLAY_TESTED — potwierdzono żądane działanie podczas testu w grze;
- STABLE_ACCEPTED — użytkownik zaakceptował konkretny przetestowany build.

Sama kompilacja, uruchomienie WoW, załadowanie DLL lub PASS w CI nie dowodzą działania funkcji. Jeśli pełny build jest zablokowany, wskaż konkretny brak i udostępnij tylko rzeczywiście przygotowany artefakt, uczciwie oznaczając jego status. Nie wymyślaj linków do plików.

## GITHUB I WERSJE
main = ostatni zaakceptowany stabilny stan; work = rozwój i eksperymenty. Pracuj na work. Przed nową iteracją upewnij się, że work bazuje na aktualnym main; zabezpiecz rozbieżny stan przed uporządkowaniem branchy. Nie twórz nowej stabilnej wersji dla każdej próby.

Po akceptacji dokładnie przetestowanego buildu: promuj go do main, przygotuj baseline i rollback, uaktualnij CURRENT.json, runtime/current.json, SHA256, zależności i dokumentację; uruchom weryfikatory, po czym zsynchronizuj work z main. Nie proś użytkownika o ręczne merge, rebase ani commity.

## KOD I KOMPATYBILNOŚĆ
Pracuj wyłącznie dla WoW 3.3.5a build 12340 Windows x86. Przed pierwszą zmianą zależną od klienta potwierdź aktywny EXE narzędziem `python tools/verify_reference_client.py --path <Wow.exe>` i odpowiednim audytem binarki. Nie przenoś automatycznie kodu, offsetów, struktur ani hooków z WoW 1.12 lub innych buildów.

`src/` to canonical editable source root. Gdy runtime/current.json wskazuje source_path/canonical_source, używaj dokładnie tego pliku; nie zgaduj po nazwie DLL. Archiwa służą do recovery i rollbacku, a reconstructed source nie jest original source. Wprowadzaj punktowe zmiany i chroń działające funkcje.

## BUILD, UPDATER I PACZKI
GitHub Actions powinien automatycznie kompilować zmienione komponenty Windows x86, uruchamiać właściwe testy, sprawdzać PE32/I386, ABI, SHA256, zgodność zależności oraz publikować **kompletny ZIP** do pobrania. Aktualizuj odpowiedni workflow przy zmianach modułu; preferuj szybkie kompilacje i nie twórz zbędnych pipeline'ów. Zielony workflow nie zastępuje testu w grze.

Paczka zawiera tylko potrzebne pliki, w tym zgodne DLL, loader/EXE i konfigurację. Jeśli zawiera WoW.exe, umieść go w głównym katalogu ZIP-a obok DLL. Przed publikacją sprawdź strukturę, integralność i kompletność paczki. Updater powinien kontrolować SHA256, zależności, niekompletne aktualizacje i rollback; nie instaluj niezgodnych zestawów ani nie nadpisuj plików używanych przez uruchomioną grę.

## STABILNOŚĆ I WERYFIKACJA
Stabilność uruchamiania i działania gry jest warunkiem dostarczania funkcji. Kontroluj kolejność ładowania, hooki, wątki, wskaźniki, cykl życia obiektów i zależności. Przy crashu ustal przyczynę, także w ostatnio zmienionych zależnościach; rollback nie zastępuje naprawy. Loader powinien umożliwiać raporty z wersjami EXE/DLL, logami i dostępnymi danymi wyjątku.

Po każdej istotnej zmianie uruchom `python tools/verify_current.py`; przy zmianie repozytorium, baseline'u, recovery i promocji stabilnej wersji także `python tools/verify_repo.py`. Sprawdzaj manifesty, canonical source, aktywny runtime, architekturę, SHA256, zależności i kompletność buildu. Naprawiaj przyczynę błędu, nie osłabiaj verifiera dla PASS. Nie udostępniaj nieweryfikowanego buildu jako gotowego do gry; wyjątek to jasno oznaczony eksperyment diagnostyczny.

## KOMUNIKACJA
Nie pytaj o dane, które można samodzielnie ustalić w GitHubie, plikach projektu i istniejących artefaktach. Po zmianie podaj krótko: faktycznie wdrożoną funkcję lub blokadę, moduł, link do istniejącego ZIP/DLL, wynik weryfikacji, status work/main i konkretny test w grze.

„napraw” = diagnozuj, popraw, zweryfikuj i przygotuj paczkę; „rozbuduj” = dodaj funkcję bez regresji; „optymalizuj” = przyspiesz bez łamania kompatybilności; „przeanalizuj” = zacznij od aktualnego GitHuba; „wrzuć na GitHub” = wykonaj zmianę; „buduj”/„daj paczkę” = dostarcz gotowy artefakt; „stabilne”/„akceptuję” = promuj zaakceptowany, zweryfikowany build.

**Najważniejszy miernik projektu: czas od zgłoszenia do rzeczywiście działającej DLL gotowej do testu.** Aktualne instrukcje repozytorium mają pierwszeństwo przed starszymi rozmowami.