# Radiance-AMB

Motor granular continuo de Javhastudios: app independiente para Windows y macOS y plugin
VST3 / AU, a partir del prototipo web (`web/radiance-amb.html`).

Se compila solo en GitHub (pestaña **Actions**). En cada ejecución, en **Artifacts**:

- `RadianceAMB-Windows` → `Radiance-AMB.exe` + `Radiance-AMB.vst3`
- `RadianceAMB-macOS` → `.dmg` con la app, el VST3 y el AU (Intel y Apple Silicon, macOS 10.13+)
- `RadianceAMB-Linux` → app y VST3
- `capturas-interfaz` → capturas de la interfaz hechas en la prueba automática

Las instrucciones de instalación van en el `LEEME.txt` de cada paquete.

## Qué hace igual que la web

- El mismo motor: granos con su envolvente, posición fija / LFO (Hz o sincronizado) / deriva,
  filtro con dispersión, tono con deriva, ADSR por nota, movimiento, dispersión estéreo,
  reverb de convolución con pre-delay, Space y shimmer, los 6 materiales de fábrica.
- MIDI: notas con velocidad, pitch bend (con su rango), rueda de modulación (vibrato) y pedal.
- Teclado en pantalla de 2 octavas y teclado del ordenador (`A W S E D F T G Y H U J K O L P ;`).
- Librería con tu propio audio, ganancia por material, patches en el mismo `.json` que exporta
  la web (también los que llevan el audio dentro), Aleatorizar y grabación a WAV estéreo 16 bits.

## Diferencias

- Los granos salen justo a tiempo (la web los programaba con 0,2 s de antelación cada 30 ms).
- Dry/Wet y Volumen se suavizan unos milisegundos para que no hagan clic.
- No están la carga/reproducción de archivos .mid ni el botón "Conectar controlador": en la app
  el teclado MIDI se elige en Options y en el DAW llega por la pista.
- Los archivos viven en `Documentos/Radiance-AMB` (`Libreria`, `Patches`, `Grabaciones`). El
  proyecto del DAW guarda también el audio propio que estés usando.

## Comprobación del motor

`tests/run_js.mjs` ejecuta el prototipo original en Node con
[node-web-audio-api](https://github.com/ircam-ismm/node-web-audio-api) (mismas acciones,
mismos números aleatorios) y `tests/compare_cpp.cpp` repite lo mismo con el motor en C++, en un
modo que imita el planificador de la web. `tests/compare.mjs` compara: todos los escenarios
deben coincidir con más de 40 dB de relación señal/diferencia (salen entre 60 y 87 dB).

```
cd tests && npm install --no-save node-web-audio-api@2.2.0 && cd ..
node tests/run_js.mjs out
g++ -std=c++17 -O2 -o compare_cpp tests/compare_cpp.cpp && ./compare_cpp out
node tests/compare.mjs out
```

Fuentes: Raleway, JetBrains Mono y Audiowide (licencia SIL OFL, en `Resources/`).

© 2026 Javhastudios
