# Referencias de diseño gráfico aprobadas

Esta carpeta guarda las referencias **visuales** aprobadas del producto.

- **Para qué sirven:** son referencias para la implementación futura de la
  interfaz.
- **Qué no son:** no son assets de producción y no forman parte de las builds.
- **No se modifican:** no se editan, recomprimen ni transforman.

## LABQ8_UI_DESIGN_APPROVED_01

| Campo | Valor |
|---|---|
| Nombre oficial | `LABQ8_UI_DESIGN_APPROVED_01` |
| Archivo en el repositorio | [LABQ8_UI_DESIGN_APPROVED_01.png](LABQ8_UI_DESIGN_APPROVED_01.png) |
| Estado | **Diseño gráfico aprobado por el responsable del proyecto. Referencia visual congelada (VISUAL REFERENCE LOCKED).** |
| Origen | Proceso de diseño independiente, fuera de Claude Code |
| Fecha de registro | 29-10-2026 |
| Dimensiones | 1672 × 941 px, PNG RGBA de 8 bits (alfa opaco), sin entrelazado, perfil sRGB IEC61966-2.1 |
| Tamaño | 2 632 581 bytes |
| SHA-256 (**archivo maestro**) | `4e14b9254d59589deff406b9fdc485f013e24883fd30c024e57c59ba55f30b01` |
| Archivo original | `Ecualizador vintage LABQ8 en verde retro(1).png` |
| Copia | Byte a byte (`cp -p`, verificada con `cmp`) desde `~/Downloads/Ecualizador vintage LABQ8 en verde retro(1).png`. El hash coincide con el del encargo. |

**Este archivo es la única referencia maestra.**

### Equivalencia con la copia registrada antes (sustituida)

**Primera copia (29-10-2026):**

- archivo: `Ecualizador vintage LABQ8 en verde retro.png`;
- SHA-256: `db9fd396c0e0f48ebbbb721f1a8fa06276e5e419afe345b6440024857120956a`;
- formato: 2 198 957 bytes, PNG RGB de 8 bits sin alfa y sin perfil de color.

Se registró así porque el maestro no estaba disponible todavía.

**Comprobación de la equivalencia (29-10-2026):**

- el responsable del proyecto confirmó por comparación píxel a píxel que los
  dos archivos contienen la misma imagen;
- se repitió la comparación aquí: ambos se decodificaron con ImageIO a RGBA
  sRGB de 8 bits y salieron **0 bytes distintos de 6 293 408**;
- la diferencia de hash viene de la codificación PNG (canal alfa opaco,
  perfil sRGB incrustado, compresión) y de los metadatos, no de la imagen.

**Sustitución:** la copia anterior se reemplazó por el maestro y ya no se
conserva en el repositorio.

### Elementos visuales congelados

**Chasis**

- rack horizontal de estética profesional de los años ochenta, en verde
  corporativo VitaPlug envejecido;
- texturas de desgaste, arañazos y acabado metálico;
- asas laterales, tornillería y **subrack negro inferior**;
- proporciones y distribución originales.

**Identidad**

- a la izquierda: logotipo LABQ8, "VitaPlug", "DYNAMIC EQUALIZER", "8 BANDS"
  y "PRECISION · MUSICAL · ANALOG SOUL";
- a la derecha: "designed by SerxDev" y "VITALAB";
- se conservan la disposición, la jerarquía tipográfica y el equilibrio.

**Pantalla central**

- pantalla oscura con iluminación verde;
- gráfica de respuesta con ocho nodos de colores;
- curvas estática y dinámica, y analizador espectral;
- pestañas EQ / SPECTRUM / DYN / ALL, "Analyzer", "View" y ajustes;
- medidores IN/OUT a la derecha;
- se conservan el tamaño, las proporciones, los marcos y la ubicación.

**Selector de bandas**

- ocho botones horizontales, numerados y de colores, bajo la pantalla;
- seleccionan la banda que se edita en el panel inferior.

**Panel compartido** (uno solo para las ocho bandas)

- FILTER TYPE, FREQUENCY, GAIN, Q, interruptor DYNAMIC EQ, THRESHOLD,
  RANGE, ATTACK, RELEASE y PHASE BEHAVIOR;
- potenciómetros metálicos con escala y display inferior.

**Controles globales**

- STEREO MODE (STEREO / M/S / L/R), POLARITY e INTENTION.

### Diseño aprobado no es funcionalidad autorizada

La aprobación es visual. No autoriza a implementar el DSP de funciones
pendientes o en investigación:

- procesamiento M/S avanzado;
- INTENTION y los presets inteligentes;
- PHASE BEHAVIOR (experimental).

Cualquier incompatibilidad entre la imagen y el DSP real se documenta y se
somete a decisión antes de actuar.

**Diferencias ya detectadas** (rangos mostrados en la imagen frente al DSP en
`5623dc5`), sin decidir:

| Control | Imagen | DSP actual |
|---|---|---|
| GAIN | −18 … +18 dB | −24 … +24 dB |
| Q | 0,1 … 10 | 0,1 … 18 (guardado; efectivo según tipo, E1b.1) |
| THRESHOLD | −60 … 0 dB | −80 … +24 dBFS (interno, E2a/E2b) |
| RANGE | 0 … 24 dB | 0 … 24 dB |
| ATTACK | 0,1 … 200 ms | 0,5 … 500 ms (interno) |
| RELEASE | 10 … 2000 ms | 5 … 5000 ms (interno) |

### Reglas para la fase de interfaz (cuando se autorice)

1. Reproducir fielmente el diseño aprobado.
2. No inventar controles ni reorganizar los existentes.
3. No sustituir los potenciómetros.
4. No alterar chasis, colores, texturas ni proporciones.
5. Mantener la arquitectura de panel compartido.
6. Separar assets gráficos, componentes interactivos y DSP.
7. La selección visual de bandas no modifica el audio, ni reinicia el
   procesamiento de ninguna banda.
8. Conservar los identificadores de parámetros y la automatización de JUCE.
9. Ningún cambio de interfaz puede producir regresiones DSP.
10. Cualquier desviación gráfica necesaria requiere aprobación expresa.

**Coordenadas:** la imagen es una referencia de producto, no un plano técnico.
Las coordenadas, las zonas de clic y las dimensiones de cada componente se
medirán y documentarán antes de implementar.
