# Estado del proyecto — Radiance-AMB (y plan común de Javhastudios)

> Para retomar en una conversación nueva: "lee el ESTADO.md de radiance-amb".
> Actualizado: 20/11/2026. Copia de este archivo y material de trabajo (bocetos, diseños) en el
> proyecto de Claude "Javhastudios Plugins".

## Cómo trabajamos

- El usuario (Javhastudios) habla español, sin jerga. No compila nada en local: todo se compila en
  GitHub Actions con JUCE 8 (FetchContent, 8.0.10).
- La receta de conversión web → app + VST3/AU está en la habilidad guardada
  **web-audio-to-juce-plugin** (estructura, comparación con el original, CMake, workflow, ayuda Mac).
- Seguimiento de compilaciones sin `gh`: `curl https://api.github.com/repos/Javhastudios/<repo>/actions/runs`.

## Repositorios (cuenta Javhastudios)

| Repo | Qué es | Estado |
|---|---|---|
| blue-harvest | WalkerMorph JV → Blue Harvest (morphing espectral) | Terminado, funciona en Mac (Live) |
| blade-rain | Emulador VP-330 (strings + vocoder) | Terminado (último arreglo: micro en la app standalone, commit a78a748) |
| radiance-amb | Motor granular continuo | Versión 1 terminada, pendiente de rediseño (ver abajo) |

Los tres son públicos ahora mismo. Antes de vender: hacerlos privados.

## Radiance-AMB: lo que hay (versión 1)

- App + VST3 + AU (Mac), Windows y Linux. Última compilación correcta:
  https://github.com/Javhastudios/radiance-amb/actions/runs/44724638864
- Motor en C++ (`Source/dsp/RadianceEngine.h`, `Factory.h`, `WebAudio.h`) comprobado contra el HTML
  original (`web/radiance-amb.html`) con node-web-audio-api 2.2.0: 5 escenarios, 60–87 dB de SNR.
  Tiene un modo `webCompat` que imita el planificador de la web (solo para las pruebas); en el
  plugin los granos salen justo a tiempo.
- Interfaz actual: réplica vertical de la web (con scroll), en español, con teclado en pantalla.
- Librería y patches: `Documentos/Radiance-AMB/{Libreria,Patches,Grabaciones}`. Los patches son el
  mismo .json que la web. El proyecto del DAW guarda también el audio propio en uso.
- Pruebas: `tests/smoke.cpp` (todo el plugin + capturas) y pluginval nivel 5, ambas en CI.

## Decisiones tomadas para la versión 2

1. **Interfaz en inglés** (textos, nombres de parámetros en el DAW, materiales, presets de fábrica,
   carpetas `Library/Patches/Recordings`). Los ids internos no cambian: los .json de la web siguen valiendo.
   LEEME de instalación: en español salvo que se pida otra cosa (pendiente de confirmar).
2. **Disposición horizontal, sin teclado en pantalla**, ventana 1280×720 escalable. Boceto:
   - Online (canvas, en inglés): https://claude.ai/artifact/Wjy8ztFkfXFyXADLdHbhyF
   - PDF (inglés y español): en el proyecto de Claude "Javhastudios Plugins" (no en el repositorio).
   - Zonas: A barra de presets · B librería de audio · C visor de onda/posición · D Source (posición)
     · E–J módulos Grain, Note, Pitch, Timbre, Space, Reverb. El panel Output desaparece: Play,
     Record y Volume van a la barra; el rango de pitch bend va a Pitch.
   - Se sigue tocando por MIDI y con el teclado del ordenador (A W S E D…).
3. **Presets como en los plugins comerciales**: barra superior (◀ ▶, nombre con * si cambiado,
   Save, Save as, Randomize) y navegador (categorías, favoritos, buscador, fábrica/míos, importar y
   exportar .json, renombrar, borrar; ↑↓ para ir probando). Presets de fábrica dentro del plugin y
   visibles en el menú de presets del DAW. Pendiente: ¿los crea el usuario o una primera tanda de
   20–30 hecha por Claude para revisar?
4. **Venta**: web propia + **Moonbase** (tienda, cobro, IVA como vendedor oficial, licencias con
   módulo JUCE, prueba con fecha límite; 5 % + 1 € por venta). Lemon Squeezy descartado (cierra el
   31/03/2027). Alternativa barata: Polar (sin módulo para plugins).

## Próximos pasos, en orden

1. **El usuario** entrega el diseño final: PNG a tamaño real (1280 px), fuentes .ttf/.otf con
   licencia para incrustar, piezas sueltas en PNG transparente (fondos, logo, potenciómetro o tira
   de posiciones), estado del navegador de presets y, opcional, la ventana "Save as".
2. **Claude** implementa la versión 2 de una vez: diseño nuevo + inglés + presets (barra y
   navegador) + presets de fábrica. Rehacer capturas en el smoke test y comprobar contra el PNG.
3. **El usuario** crea cuenta en Moonbase (modo prueba). Claude integra la activación (módulo
   `moonbase_licensing`) y decide con él: nº de ordenadores (2–3), prueba de 14 días, qué pasa sin
   licencia.
4. **Claude** hace la web (GitHub Pages + dominio propio): página del plugin, compra integrada,
   descargas e instrucciones. Sirve luego de catálogo para todos los plugins.
5. Antes de vender:
   - Firma oficial de Apple (Apple Developer, 99 USD/año) + notarización en el workflow con secretos.
   - Instaladores (.pkg Mac, .exe Windows) en lugar de zip/dmg + LEEME.
   - Repos privados (ojo: minutos de Actions limitados; macOS gasta más).
   - Revisar licencia de JUCE para venta y el papeleo en España (gestor).

## Ofrecido y no pedido todavía

- Más controles del vocoder en Blade Rain.
- Release automática (etiqueta `v*`) para descargas sin iniciar sesión en GitHub.
