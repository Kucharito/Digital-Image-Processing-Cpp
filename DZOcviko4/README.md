# DZO cvičenie 4 – Diskrétna Fourierova transformácia

Riešenie [zadania Discrete Fourier Transform](https://geordi.github.io/cv-courses/dft_en.html)
v C++ s OpenCV. Výpočet DFT je implementovaný vlastnými súčtami; nepoužíva sa
`cv::dft`, FFT ani iná hotová transformácia. OpenCV zabezpečuje načítanie,
zmenšenie, uloženie a zobrazenie obrázkov.

## Spustenie

Program je skompilovaný v `build/dzo_dft.exe`. V PowerShelli:

```powershell
cd D:\DZOall\DZOcviko4
.\run.cmd
```

Spúšťač nastaví cestu k MSYS2/OpenCV DLL a pracovný priečinok projektu.
Predvolene sa použije sivá Lena z materiálov kurzu, zmenšená na **64 × 64**.
Program overí implementáciu, porovná obe DFT, vypíše tabuľku časov a otvorí
okná **Image**, **Phase**, **Power** vedľa seba. Amplitúda je v štvrtom okne
pod nimi. Stlačením ľubovoľnej klávesy v obrazovom okne program ukončíte.

Výsledky bez otvorenia okien:

```powershell
.\run.cmd --no-show
```

Zobrazenie bližšie k screenshotu „Expected Output“:

```powershell
.\run.cmd --reference-style
```

Vlastný obrázok a iná veľkosť:

```powershell
.\run.cmd "D:\obrazky\vstup.png" --size 128
```

Zachovanie pôvodných rozmerov vrátane obdĺžnikových obrázkov:

```powershell
.\run.cmd "D:\obrazky\vstup.png" --original-size
```

Relatívne cesty k obrázkom sa vyhodnocujú voči `DZOcviko4`. Pre väčšie obrázky
sa výsledný obraz počíta separabilnou DFT; plná priama DFT sa porovnáva len
pri najviac 4096 pixeloch. Povinné porovnania na malých obrazoch a merania
8, 16, 32 a 64 sa vykonávajú aj pri väčšom vstupnom obrázku. Separabilná DFT
nie je FFT, takže pri veľkých rozmeroch tiež môže trvať dlhšie.

Samostatné overenie alebo vynechanie tabuľky časov:

```powershell
.\run.cmd --self-test
.\run.cmd --no-benchmark --no-show
```

## Čo sa počíta

Fourierova transformácia rozloží obraz na priestorové frekvencie. Nízke
frekvencie zodpovedajú pomalým zmenám jasu, vyššie frekvencie jemným detailom
a hranám. Výsledkom nie je rozmazaný obraz, ale **komplexné spektrum** rovnakej
veľkosti ako spracovaný vstup.

`M` znamená počet riadkov a `N` počet stĺpcov. Vstupné pixely sú `double`
v rozsahu 0–255 a spektrum používa `std::complex<double>`.

### Priama 2D DFT

Funkcia `dft2DDirect()` realizuje definíciu štyrmi vnorenými cyklami:

```text
F(k,l) = 1/sqrt(M*N) * sum_m sum_n
         f(m,n) * [cos(angle) + i*sin(angle)]
angle = -2*pi*(m*k/M + n*l/N)
```

Každý výstupný koeficient závisí od všetkých vstupných pixelov. Zložitosť je
`O(M²N²)`, pre štvorcový obraz `O(N⁴)`.

### Separabilná 2D DFT

`dft1D()` prijíma komplexný vektor a počíta vlastný súčet s normalizáciou
`1/sqrt(L)`. `dft2DSeparable()` ju najprv zavolá na každý riadok a potom na
každý stĺpec medzivýsledku. Stĺpce musia zostať komplexné; imaginárnu časť
nemožno zahodiť.

Výsledná normalizácia je `1/sqrt(N) * 1/sqrt(M) = 1/sqrt(M*N)`, teda rovnaká
ako v priamej DFT. Zložitosť klesne na `O(MN(M+N))`, pre štvorcový obraz
`O(N³)`. Rozmery nemusia byť mocninami dvojky.

### Zobrazenie podľa aktuálneho zadania

`visualize()` používa výsledok separabilnej DFT:

- **Amplitúda:** `abs(F) = sqrt(real² + imaginary²)`, na zobrazenie škálovaná
  do 0–255. Silná stredová zložka môže zatieniť ostatné frekvencie.
- **Fáza:** `atan2(imaginary, real)`, rozsah `[-pi, pi]` sa mapuje na 0–255.
  Pre presne nulový koeficient sa použije fáza 0, teda stredná sivá.
- **Výkon:** `P = abs(F)²`; zobrazuje sa `log(1 + P)`, normalizované na 0–255.
  Ak sú všetky zobrazované hodnoty rovnaké, výsledkom sú nuly.

Pred zobrazením `fftshift()` posunie celý komplexný koeficient o `floor(M/2)`
riadkov a `floor(N/2)` stĺpcov. Nulová frekvencia sa tým dostane do stredu.
Funguje to aj pre nepárne rozmery bez odrezávania riadkov či stĺpcov.
Pôvodné neposunuté spektrum zostáva zachované a uloží sa pre prípadnú ďalšiu
prácu. Pri porovnávaní DFT sa používa toto pôvodné komplexné spektrum,
nie obrázky po normalizácii.

### Vzhľad referenčného screenshotu

Ukážka na stránke obsahuje tri panely **Image – Phase – Power**. Projekt
vytvára rovnaké usporiadanie a zväčšuje 64 × 64 pixely na 256 × 256 pomocou
najbližšieho suseda, aby zostali viditeľné jednotlivé pixely.

Pri porovnaní screenshotu s vypočítanými variantmi jeho fáza lepšie
zodpovedá zobrazeniu uhla modulo `pi` než plnej fáze `atan2`; ide o odhad
podľa obrázka. Súčasný text zadania však výslovne vyžaduje `atan2` aj
`log(1 + P)` s unitárnou normalizáciou DFT.

Preto `phase.png`, `power.png` a `result_images.png` vždy dodržiavajú aktuálne
rovnice. Doplnkový `reference_style.png` používa uhol zložený do
`[-pi/2, pi/2]` a výkon zobrazený po odobratí unitárnej mierky. Tento náhľad
možno otvoriť aj prepínačom `--reference-style`. Neovplyvňuje vypočítané
komplexné spektrum ani overenie. Je to vizuálne priblíženie screenshotu;
identita každého pixelu nie je overená, pretože jeho presné vstupné dáta
a nastavenia zobrazenia nie sú v zadaní uvedené.

## Výstupné súbory

Všetky vznikajú v `DZOcviko4`; ďalšie spustenie ich prepíše.

| Súbor | Obsah |
|---|---|
| `image.png` | Spracovaný sivý vstup, predvolene 64 × 64 |
| `amplitude.png` | Posunutá amplitúda |
| `phase.png` | Posunutá fáza podľa `atan2` |
| `power.png` | Posunuté normalizované `log(1 + P)` |
| `result_images.png` | Tri panely podľa aktuálnych rovníc |
| `reference_style.png` | Tri panely s doplnkovým zobrazením v štýle ukážky |
| `spectrum.yml.gz` | Neposunuté reálne a imaginárne časti v presnosti `double` |
| `benchmark.csv` | Namerané časy a rozdiely komplexných spektier |

## Overenie a časy

Pri každom spustení prebehnú malé numerické kontroly:

- zhoda priamej a separabilnej DFT na obraze 3 × 5,
- impulz mimo stredu porovnaný s analyticky vypočítaným spektrom,
- konštantný obraz: `F(0,0) = c*sqrt(M*N)`, ostatné koeficienty približne 0,
- komplexný vstup 1D DFT a zachovanie energie unitárnou transformáciou,
- posun párnych aj nepárnych obdĺžnikových spektier,
- nulový obraz: bezpečná normalizácia amplitúdy/výkonu a fáza 0.

Príklad nameraný na tomto počítači v zostavení Release:

| Rozmer | Priama DFT [ms] | Separabilná DFT [ms] | Zrýchlenie |
|---|---:|---:|---:|
| 8 × 8 | 0.266 | 0.077 | 3.46× |
| 16 × 16 | 4.208 | 0.494 | 8.51× |
| 32 × 32 | 66.626 | 3.987 | 16.71× |
| 64 × 64 | 1022.643 | 34.910 | 29.29× |

Pri obraze 64 × 64 bol maximálny rozdiel komplexných spektier približne
`3.18e-11`. Z merania je vylúčené načítanie, zmenšovanie, vizualizácia,
porovnanie a ukladanie. Meria sa samotná transformácia vrátane jej
pracovných alokácií. Časy sa pri ďalšom spustení môžu meniť.

Zdvojnásobenie strany znamená teoreticky **16× viac práce** pre priamu DFT
a **8× viac** pre separabilnú DFT. Malé rozmery ovplyvňuje réžia merania
a veľké aj stav procesora, preto namerané pomery nie sú presne 16 a 8.

## Kompilácia

Potrebné sú MSYS2 MinGW64, CMake, Ninja a OpenCV 5 v `C:\msys64\mingw64`.
V termináli **MSYS2 MinGW64**:

```bash
cd /d/DZOall/DZOcviko4
cmake -S . -B build -G Ninja \
  -DOpenCV_DIR=C:/msys64/mingw64/lib/cmake/opencv5 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Štruktúra a zdroje

```text
DZOcviko4/
├── main.cpp                     # obe DFT, overenie, meranie a zobrazenie
├── CMakeLists.txt               # zostavenie programu
├── run.cmd                      # spúšťač pre PowerShell
├── README.md
├── images/lena_gray.png         # sivá Lena z materiálov kurzu
├── reference/expected_output.png # pôvodný screenshot zo zadania
└── build/dzo_dft.exe            # skompilovaný program
```

Vstup: [lena_gray.png](https://geordi.github.io/cv-courses/images/intro/lena_gray.png).
Referencia: [Expected Output](https://geordi.github.io/cv-courses/images/dft/result_images.png).
