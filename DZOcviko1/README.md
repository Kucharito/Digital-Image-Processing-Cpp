# DZO cvičenie 1 – Základy OpenCV

Prvé cvičenie ukazuje základnú prácu s obrázkami v jazyku C++ pomocou
knižnice OpenCV. Samotný projekt sa nachádza v priečinku `dzo_vsc`.

## Čo program robí

Program načíta farebný obrázok `images/lena.png` a následne:

1. skontroluje, či sa obrázok podarilo načítať,
2. prevedie farebný obrázok na odtiene sivej,
3. vytvorí 8-bitovú aj 32-bitovú verziu sivého obrazu,
4. prečíta a vypíše hodnoty vybraného pixelu,
5. zmení jeden pixel na čierny,
6. nakreslí do sivého obrázka vyplnený obdĺžnik,
7. vytvorí horizontálny gradient od čiernej po bielu,
8. zobrazí vytvorené obrázky v samostatných oknách.

## Reprezentácia obrázkov

Program používa triedu `cv::Mat`:

- `CV_8UC3` je farebný obraz s tromi 8-bitovými BGR kanálmi,
- `CV_8UC1` je sivý obraz s hodnotami od 0 do 255,
- `CV_32FC1` je sivý obraz s desatinnými hodnotami od 0.0 do 1.0.

Farebný obraz sa prevedie na sivý pomocou `cv::cvtColor`. Následne sa pomocou
`convertTo` vytvorí desatinná verzia, pričom sa hodnoty vydelia číslom 255.

K jednotlivým pixelom sa pristupuje pomocou `Mat::at`. Súradnice sa zapisujú
v poradí `(y, x)`, teda najskôr riadok a potom stĺpec.

## Vytvorený gradient

Program vytvorí obrázok s rozmermi 256 × 50 pixelov. Hodnota každého pixelu sa
nastaví podľa jeho vodorovnej súradnice `x`. Naľavo je preto hodnota 0 (čierna)
a napravo hodnota 255 (biela).

## Zobrazené okná

Po spustení sa zobrazia tri okná:

- `Gradient`,
- `Lena gray`,
- `Lena gray 32f`.

Okná zostanú otvorené približne jednu sekundu, pretože program používa
`cv::waitKey(1000)`.

## Spustenie

V PowerShelli najskôr prejdite do priečinka projektu:

```powershell
cd D:\DZOall\DZOcviko1\dzo_vsc
```

Potom spustite pripravený program:

```powershell
.\build\dzo.exe
```

Program je potrebné spúšťať z priečinka `dzo_vsc`, pretože vstupný obrázok
načítava relatívnou cestou `images/lena.png`.

## Kompilácia

Projekt používa CMake a OpenCV. Po zmene zdrojového kódu ho možno zostaviť:

```powershell
cd D:\DZOall\DZOcviko1\dzo_vsc
cmake -S . -B build
cmake --build build
```

Výsledný spustiteľný súbor sa vytvorí v `build/dzo.exe`.

## Štruktúra

```text
DZOcviko1/
├── README.md
└── dzo_vsc/
    ├── images/
    │   └── lena.png
    ├── build/
    │   └── dzo.exe
    ├── main.cpp
    └── CMakeLists.txt
```
