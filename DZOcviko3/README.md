# DZO cvičenie 3 – Anizotropný filter

Projekt implementuje anizotropné filtrovanie obrazu podľa zadania
[Anisotropic Filtration](https://geordi.github.io/cv-courses/anisotropic_filter.html).
Na rozdiel od Gaussovho rozmazania filter vyhladzuje šum v podobných oblastiach,
ale obmedzuje vyhladzovanie cez výrazné hrany.

## Princíp

Každý pixel je spojený so štyrmi priamymi susedmi: severným, južným, východným
a západným. Rozdiel jasov medzi stredným pixelom a susedom tvorí gradient.
Vodivosť daného spojenia je:

```text
g(gradient) = exp(-(gradient * gradient) / (sigma * sigma))
```

Malý gradient znamená podobné pixely, vysokú vodivosť a silnejšie vyhladenie.
Veľký gradient pravdepodobne predstavuje hranu. Vodivosť pri ňom klesne takmer
na nulu, takže sa jas cez hranu takmer neprenáša.

Nová hodnota pixelu sa vypočíta zo starej hodnoty a štyroch susedov:

```text
new = center * (1 - lambda * (cN + cS + cE + cW))
    + lambda * (cN*N + cS*S + cE*E + cW*W)
```

Podľa zadania program používa:

- `sigma = 0.015`,
- `lambda = 0.1`,
- obraz typu `double` (`CV_64FC1`),
- predvolene 1000 iterácií.

Každá iterácia číta zo vstupnej matice a zapisuje do druhej matice. Výpočet teda
nie je vykonávaný priamo v tej istej matici. Po iterácii sa matice vymenia.
Okrajové pixely zostávajú nezmenené, pretože nemajú všetkých štyroch susedov.
Riadky jednej iterácie sa počítajú paralelne, pričom každý riadok stále číta iba
z pôvodnej matice danej iterácie, takže výsledok zodpovedá uvedenej rovnici.

## Súbory

```text
DZOcviko3/
├── images/
│   └── input_image.png       # ukážkový vstup zo zadania
├── build/
│   └── dzo_anisotropic.exe   # skompilovaný program
├── main.cpp                  # implementácia filtra
├── CMakeLists.txt            # konfigurácia CMake
├── run.cmd                   # spúšťač pre PowerShell
└── README.md
```

Výsledok sa uloží ako `anisotropic_result.png`.

## Spustenie

V PowerShelli:

```powershell
cd D:\DZOall\DZOcviko3
.\run.cmd
```

Po dokončení sa zobrazí pôvodný a filtrovaný obraz. Okná zatvoríte stlačením
ľubovoľnej klávesy v obrazovom okne.

Spustenie bez okien:

```powershell
.\run.cmd --no-show
```

Vlastný obrázok:

```powershell
.\run.cmd "D:\obrazky\vstup.png"
```

Iný počet iterácií, napríklad 100:

```powershell
.\run.cmd --iterations 100
```

Prepínače možno kombinovať:

```powershell
.\run.cmd "D:\obrazky\vstup.png" --iterations 500 --no-show
```

## Kompilácia

V termináli MSYS2 MinGW64:

```bash
cd /d/DZOall/DZOcviko3
cmake -S . -B build -G Ninja \
  -DOpenCV_DIR=C:/msys64/mingw64/lib/cmake/opencv5 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
