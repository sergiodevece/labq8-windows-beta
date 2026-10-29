# E2c.1: parámetros públicos de la dinámica

E2c.1 conecta las ocho secciones dinámicas de E2b a 40 parámetros JUCE
públicos, con version hint 4, índices 40–79 y estado v4.

- **DSP sin cambios:** los algoritmos validados en E2a y E2b no cambian.
- **Rangos:** son los del DSP, por decisión de producto (29-10-2026).
- **Esquema completo:** en [PARAMETER_SCHEMA.md](PARAMETER_SCHEMA.md#dinámica-por-banda-e2c1-version-hint-4-índices-4079).

## 1. Arquitectura de integración

```
host ─► APVTS (80 parámetros) ─► processBlock: pushParametersToCore()
                                   ├─ banda n: BandSettings    ─► EqCore::setBandTargets (n, …)
                                   └─ banda n: DynamicSettings ─► EqCore::setBandDynamics (n, …)
                                                                     └─ DynamicSection n (E2b)
```

- **Lectura de los parámetros:**
  - el adaptador guarda punteros `std::atomic<float>*` a los cinco
    parámetros dinámicos de cada banda;
  - al empezar cada bloque, los lee y entrega a cada sección **solo los de su
    banda**.
  - No hay asignaciones, bloqueos ni cadenas en el callback.
- **Suavizado y seguridad:** es el que ya tenía el DSP.
  - Threshold y Range: rampas de 20 ms.
  - Dynamic EQ: fundido de 20 ms, sin discontinuidades.
  - Attack y Release: se aplican en el siguiente tick.
  - Solo actúa sobre Bell sin bypass.
  - La detección se hace sobre la entrada original común, con enlace
    estéreo por máximo.
- **Ruta estática:** con todas las secciones apagadas (el valor por defecto y
  todas las sesiones antiguas), `EqCore` sigue la ruta de E2b sin cambios.
  Lo comprueban el arnés estricto frente a E2b y las pruebas de transparencia.
- **Granularidad de la automatización:** el host escribe los valores entre
  bloques, como siempre en JUCE. La salida no depende del tamaño de bloque
  cuando la automatización cae en las mismas muestras (probado).

## 2. Riesgos documentados

1. **Retardo del detector en graves con Q elevada.** Medido con un seno que
   pasa de silencio a −20 dBFS; tiempo hasta leer a menos de 1 dB, en ms
   (48 kHz):

   | f0 \ Q | 0,7 | 1 | 2 | 4 | 10 | 18 |
   |---|---|---|---|---|---|---|
   | 30 Hz | 74 | 81 | 125 | 196 | 430 | **746** |
   | 60 Hz | 37 | 40 | 62 | 98 | 215 | 373 |
   | 120 Hz | 19 | 20 | 31 | 49 | 108 | 187 |
   | 250 Hz | 9 | 10 | 15 | 24 | 52 | 90 |
   | 1 kHz | 2,3 | 2,7 | 4,0 | 6,0 | 13 | 23 |
   | 5 kHz | 0,7 | 0,7 | 1,0 | 1,3 | 3,0 | 5,0 |

   Es la suma del asentamiento de los dos paso banda (≈ 2·Q/(π·f0)) y de la
   ventana RMS (≈ 1/f0). A ese retraso se suma el ataque.
2. **El umbral varía con la panorámica en estéreo enlazado.** Con el enlace
   por máximo y una panorámica de potencia constante, el punto de actuación se
   desplaza 3,01 dB en el centro (E2b). Además, el contenido de un canal
   reduce el otro (7,9 dB en la prueba de E2b).
3. **Bandas dinámicas solapadas.** Cada detector lee la señal original, así
   que dos Bells dinámicas en la misma región reaccionan las dos a la misma
   energía y sus reducciones se suman. No hay realimentación, pero el efecto
   total puede superar lo que el usuario espera de una sola banda.
4. **Artefactos de modulación.**
   - Con la interpolación de coeficientes por muestra (E2a), el residuo frente
     a un motor por muestra es de −61 dB para una banda y −113 dB para ocho
     bandas modulando a la vez (E2b).
   - Con el ataque mínimo de 0,5 ms sobre graves, la propia modulación genera
     intermodulación, como en cualquier compresor rápido.
   - La fase de la Bell se mueve con su ganancia (fase mínima).
5. **Headroom con amplificaciones importantes.**
   - La dinámica solo **reduce** la ganancia; no puede amplificar.
   - Con Bells de +24 dB y Q alto, la ganancia de peor caso es la estática:
     L1 de +26 dB con una Bell de +24 dB y Q 18 (labq8_sweep). Ocho bandas en
     cascada pueden sumar más.
   - No hay limitador ni normalización (fuera de alcance). Las señales por
     encima de 0 dBFS pasan sin recortar (probado en E2a y E2b).

## 3. Propuesta para E2c.2: Q del detector en graves

**Problema:** con Q alto en graves, la reducción llega cientos de
milisegundos tarde (tabla anterior). La selectividad que se compra con ese
retraso es la de esta tabla: rechazo a una octava de f0 del detector de 4.º
orden.

| Q del detector | Rechazo a ×2 y ×0,5 |
|---|---|
| 0,7 | −6,5 dB |
| 1 | −10,3 dB |
| 2 | −20 dB |
| 4 | −31,5 dB |
| 10 | −47 dB |
| 18 | −57 dB |

**Propuesta:** desacoplar la Q del detector de la Q del filtro y limitarla
según la frecuencia:

- **Fórmula:** Q_det = mín(Q, clamp(f0 / 30 Hz, 2, 18)).
  - A 60 Hz el máximo es Q 2: la lectura llega en 62 ms en vez de 373 ms con
    Q 18, y el rechazo a una octava es de 20 dB.
  - A 250 Hz el máximo es 8,3 (unos 45 ms).
  - Desde 540 Hz no hay límite: la banda se comporta igual que ahora.
- **Coste:** ninguno de CPU. El detector ya recalcula su región cuando
  cambian f y Q.
- **Contrapartida:** una Bell estrecha en graves reaccionaría también a
  contenido cercano, a una octava o menos.
- **Cómo evaluarlo en E2c.2:**
  - con material real: bombo y bajo, resonancias de sala;
  - midiendo el retraso y las falsas activaciones con y sin el límite;
  - comparando también con un detector de 2.º orden en graves.

Antes de E2c.2 no se introduce ningún límite automático.

## 4. Rendimiento (Release arm64, Apple M5)

**Condiciones:** la máquina estaba más cargada que en E2b; el mismo binario E2b
mide ahora cerca del doble que en su sesión. Por eso solo valen las
comparaciones **intercaladas**, en la misma sesión.

**Núcleo de 8 bandas** (`labq8_bench --e2b`, E2b y E2c.1 alternados dos veces;
% de un núcleo, 96 kHz, bloque 64):

| Escenario | E2b | E2c.1 |
|---|---|---|
| 8 estáticas | 0,60–0,61 | 0,60–0,65 |
| 8 con dinámica apagada | 0,64–0,66 | 0,63–0,66 |
| 1 dinámica | 0,83–0,88 | 0,81–0,82 |
| 4 dinámicas | 1,12–1,22 | 1,13–1,27 |
| 8 dinámicas | 1,73 | 1,63–1,90 |
| 8 dinámicas + automatización intensa | 1,94–2,11 | 1,91–2,05 |

- **Resultado:** a la par, porque el núcleo no ha cambiado. Las tablas
  completas a 44,1, 48 y 96 kHz con bloques de 64 y 512 están en
  `build/e2c1-release-universal/reports/`.

**Procesador completo** (lectura de los 80 parámetros en cada bloque):

- **Comparación con E2b:** los renderizadores del arnés de compatibilidad
  procesan las mismas 31 sesiones (dinámica apagada, 12 configuraciones cada
  una), alternando tres veces.
  - **Tiempo:** E2b tarda 5,41–5,60 s y E2c.1 5,56–5,87 s, es decir, **+0–8 %**
    (unos 2 % de mediana).
  - **Salida:** los 1147 archivos de audio son idénticos.
  - **Origen del extra:** probablemente la entrega por bloque de los ajustes
    dinámicos. Cada `setBandDynamics` recalcula dos `exp` por banda aunque no
    haya cambios.
  - **Optimización posible:** salir antes si los ajustes no cambian. No la he
    aplicado, porque no modifico el DSP sin un defecto que lo justifique.
- **Prueba `e2c perf`** (procesador, 96 kHz, bloque 64, tres pasadas):
  - 8 bandas con la dinámica apagada: 0,73–0,87 %;
  - 8 bandas dinámicas: 1,69–1,94 %;
  - el presupuesto es del 5 %.

## 5. Pruebas y compatibilidad

Ver la sección de pruebas de [ARCHITECTURE.md](ARCHITECTURE.md#pruebas).

**Arnés de compatibilidad estricto frente a E2b** (`5623dc5`): **62/62
idénticas bit a bit** en arm64 y x86_64. Las tablas frente a E0, E1a, E1b,
E1b.1 y E2a no cambian.
