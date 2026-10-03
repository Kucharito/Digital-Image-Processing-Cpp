# DZO cvičenie 2 – Konvolúcia obrazu

Tento projekt demonštruje ručnú implementáciu dvojrozmernej konvolúcie obrazu
v jazyku C++ s použitím knižnice OpenCV. Hotová funkcia OpenCV `filter2D` sa na
samotný výpočet nepoužíva – hodnotu každého výstupného pixelu počíta program
pomocou vlastných vnorených cyklov.

## O čo v projekte ide

Digitálny obraz môžeme chápať ako maticu čísel. V sivom obraze každé číslo
predstavuje jas jedného pixelu. Konvolučná maska (kernel) je menšia matica,
napríklad 3 × 3 alebo 5 × 5, ktorá určuje, ako sa pri výpočte nového pixelu
skombinujú hodnoty z jeho okolia.

Konvolúcia je definovaná vzťahom:

```text
(f * h)(x, y) = sum_i sum_j f(x - i, y - j) * h(i, j)
```

Kde:

- `f` je vstupný obraz,
- `h` je konvolučná maska,
- `(x, y)` je poloha práve počítaného pixelu,
- `i` a `j` určujú polohu prvku v maske.

Pre každý pixel program vykoná tieto kroky:

1. Umiestni stred masky na spracovávaný pixel.
2. Každý pixel v okolí vynásobí príslušnou hodnotou masky.
3. Všetky súčiny sčíta.
4. Výsledný súčet zapíše do výstupného obrazu.
5. Masku posunie na ďalší pixel a postup zopakuje.

Na okrajoch obrazu by časť masky ležala mimo dostupných pixelov. Program preto
počíta iba pozície, na ktorých sa celá maska zmestí do obrazu. Nespracované
okraje zostávajú čierne. Pri maske 3 × 3 je okraj široký jeden pixel a pri
maske 5 × 5 dva pixely.

## Použité filtre

Program aplikuje tri konvolučné masky.

### Box blur 3 × 3

```text
1/9 * [ 1  1  1 ]
      [ 1  1  1 ]
      [ 1  1  1 ]
```

Každý pixel v okolí má rovnakú váhu. Výsledkom je jednoduché priemerovanie a
rozmazanie obrazu.

### Gaussian blur 3 × 3

```text
1/16 * [ 1  2  1 ]
       [ 2  4  2 ]
       [ 1  2  1 ]
```

Pixely bližšie k stredu majú väčšiu váhu. Filter preto vytvára prirodzenejšie
vyhladenie než obyčajný priemer.

### Gaussian blur 5 × 5

```text
1/256 * [ 1   4   6   4  1 ]
        [ 4  16  24  16  4 ]
        [ 6  24  36  24  6 ]
        [ 4  16  24  16  4 ]
        [ 1   4   6   4  1 ]
```

Táto maska používa väčšie okolie, a preto vytvára silnejšie vyhladenie.
Delitele 9, 16 a 256 normalizujú masky tak, aby bol súčet ich váh rovný jednej
a filter zbytočne nemenil celkový jas obrazu.

## Ako program pracuje

Program v súbore `main.cpp`:

1. načíta farebný obrázok,
2. prevedie ho na odtiene sivej,
3. prevedie hodnoty pixelov z rozsahu 0–255 na desatinné hodnoty 0.0–1.0,
4. ručne vykoná konvolúciu pre každú z troch masiek,
5. prevedie výsledky späť na 8-bitové obrázky,
6. uloží ich ako PNG súbory,
7. štandardne zobrazí pôvodný obraz aj všetky výsledky v samostatných oknách.

Vytvorené súbory:

- `box_blur_3x3.png`
- `gaussian_blur_3x3.png`
- `gaussian_blur_5x5.png`

## Požiadavky

- C++ kompilátor z MSYS2 MinGW64,
- CMake,
- Ninja,
- OpenCV 5 nainštalované v `C:\msys64\mingw64`.

## Kompilácia

Projekt je už skompilovaný v priečinku `build`. Ak ho treba skompilovať
znova, otvorte terminál **MSYS2 MinGW64** a použite:

```bash
cd /d/DZOall/DZOcviko2
cmake -S . -B build -G Ninja \
  -DOpenCV_DIR=C:/msys64/mingw64/lib/cmake/opencv5
cmake --build build
```

Výsledný program sa nachádza v:

```text
build/dzo_convolution.exe
```

## Spustenie z PowerShellu

V PowerShelli prejdite do priečinka projektu a použite pripravený spúšťač:

```powershell
cd D:\DZOall\DZOcviko2
.\run.cmd
```

`run.cmd` zabezpečí, že Windows nájde potrebné MSYS2 a OpenCV DLL knižnice.
Priame spustenie súboru `dzo_convolution.exe` bez nastavenia systémovej
premennej `PATH` môže skončiť bez výpisu s chybou chýbajúcej DLL knižnice.

Po spustení sa otvoria štyri obrazové okná. Program ukončíte stlačením
ľubovoľnej klávesy v jednom z týchto okien.

### Spustenie bez obrazových okien

Ak chcete výsledky iba uložiť do PNG súborov:

```powershell
.\run.cmd --no-show
```

### Spracovanie vlastného obrázka

Cestu k vlastnému obrázku odovzdajte ako prvý argument:

```powershell
.\run.cmd "D:\obrazky\moj_obrazok.jpg"
```

Spracovanie vlastného obrázka bez otvorenia okien:

```powershell
.\run.cmd "D:\obrazky\moj_obrazok.jpg" --no-show
```

Ak cesta nie je zadaná, program automaticky použije obrázok `lena.png` z
prvého cvičenia.

## Súbory projektu

```text
DZOcviko2/
├── main.cpp          # načítanie obrazu a ručná implementácia konvolúcie
├── CMakeLists.txt    # konfigurácia kompilácie
├── run.cmd           # spustenie z PowerShellu so správnou cestou k DLL
├── README.md         # dokumentácia projektu
└── build/            # skompilovaný program
```
