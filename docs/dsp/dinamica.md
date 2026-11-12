# Dinámica por banda

Cómo funciona la ecualización dinámica de esceQ. Código en `dsp/include/esceq/dsp/dynamics.h` (piezas) y `equalizer.h` (cómo se combinan). Las decisiones de diseño y sus motivos están en la [ADR 0006](../decisiones/0006-dinamica-por-banda.md).

## Cadena de una banda dinámica

```
entrada del EQ ─┐
                ├─► filtro del detector ─► |x| (máx. de canales) ─► envolvente ─► dB ─► curva de ganancia ─► × mezcla
sidechain ──────┘                                                                                                │
                                                                                                                 ▼
señal de la banda ─────────────────────► filtro de la banda (ganancia estática suavizada + ganancia dinámica) ─► salida
```

1. **Fuente:** la entrada del ecualizador (detección interna) o el sidechain (externa). Nunca la salida de otra banda.
2. **Filtro del detector** (`designDetector`), según el tipo de la banda:

   | Tipo de banda | Detector |
   |---|---|
   | Campana | Paso banda con la frecuencia y el Q de la banda (0 dB en el centro) |
   | Shelf de graves | Paso bajo Butterworth de 12 dB/oct en la frecuencia de la banda |
   | Shelf de agudos | Paso alto Butterworth de 12 dB/oct |
   | Tilt | Sin filtro: banda completa |
   | Resto | La dinámica no actúa |

   La ganancia de la banda no influye en el detector.
3. **Nivel:** valor absoluto de la señal filtrada. El detector escucha la misma parte de la señal que procesa la banda ([ADR 0007](../decisiones/0007-canal-por-banda.md)):

   | Canal de la banda | Qué escucha el detector |
   |---|---|
   | Stereo | El máximo de los dos canales (detección enlazada: la imagen estéreo no se mueve) |
   | Left / Right | Ese canal de la fuente |
   | Mid / Side | La suma `(L+R)/2` o la diferencia `(L−R)/2` de la fuente |

   Vale igual para la detección interna y para el sidechain externo. Una fuente mono se escucha tal cual.
4. **Envolvente** (`EnvelopeFollower`): detector de pico con ataque y relajación separados (Giannoulis, Massberg y Reiss, 2012):

   ```
   c = nivel > env ? e^(−1/(ataque·fs)) : e^(−1/(relajación·fs))
   env = nivel + c · (env − nivel)
   ```

   Ante un escalón, alcanza el 63 % en un tiempo de ataque y cae al 37 % en un tiempo de relajación. Con ataques lentos, la envolvente de un seno queda algo por debajo de su pico (con 1 ms a 1 kHz, unas décimas de dB).
5. **Curva de ganancia** (`dynamicGainDb`), con nivel `L`, umbral `T`, rodilla `W` y rango `R`, todo en dB:

   ```
   exceso = L − T
   y = 0                         si exceso ≤ −W/2
   y = (exceso + W/2)² / (2·W)   si |exceso| < W/2
   y = exceso                    si exceso ≥ W/2
   ganancia = signo(R) · min(y, |R|)
   ```

   La curva es continua y derivable salvo en el límite del rango. Con `R` negativo la banda baja hasta dejar su zona del espectro en el umbral (limitación por banda); con `R` positivo la sube lo mismo que el exceso (expansión hacia arriba).
6. **Aplicación:** la ganancia, multiplicada por la mezcla (0..1), se **suma** a la ganancia estática ya suavizada, y la banda se rediseña con `designBand`. La curva que dibuja la interfaz usa la misma suma.

## Tiempo real y ausencia de chasquidos

| Qué | Cómo | Por qué |
|---|---|---|
| Ganancia dinámica | Se evalúa en cada muestra; los coeficientes se recalculan cuando cambia ≥ 0,001 dB, o al volver a 0 dB exactos | Recalcular cada 8 muestras daba saltos de hasta 2,5 veces el paso de un seno limpio con ataques de 1 ms |
| Umbral, rango, rodilla | Suavizado de un polo **por muestra**, 15 ms | La ganancia los sigue 1:1: en escalones cada 32 muestras se oía un "zipper" |
| Ataque, relajación, fuente | Inmediatos | La envolvente es continua: cambiar sus constantes no produce saltos |
| Activar o desactivar la dinámica | Fundido lineal de la mezcla, 10 ms | Volver de −24 dB a 0 dB de golpe es un chasquido |
| Cambiar a un tipo sin ganancia | El fundido de tipo de la fase 2 (ver [ecualizador.md](ecualizador.md)) | |
| Por debajo de la rodilla | Ganancia 0 sin calcular el logaritmo | Ahorra CPU en el caso habitual |

El filtro SVF admite cambios de coeficientes en cada muestra sin volverse inestable (ver [filtros.md](filtros.md)), que es lo que permite este diseño. Todo se decide muestra a muestra a partir de la señal, así que el resultado no depende del tamaño de bloque del host.

Al arrancar la dinámica (activarla, salir del bypass, `reset()`), el detector y la envolvente empiezan desde cero y la ganancia crece con el tiempo de ataque.

## Verificación

`tests/dsp/dynamics_test.cpp` comprueba:

- La curva de ganancia: valores conocidos, rango con signo, continuidad y derivabilidad en la rodilla, entradas no finitas.
- La envolvente: constantes de tiempo de ataque y relajación exactas.
- El detector: 0 dB en el centro de la campana, paso bajo y paso alto en los shelves.
- Un seno por encima del umbral se reduce lo que dice la curva (±0,1 dB), y la ganancia dinámica se suma a la estática.
- Por debajo del umbral, o con energía fuera de la zona de la banda, la salida es idéntica bit a bit a la de la banda estática.
- Un de-esser sobre un tono grave con ráfagas a 7 kHz: las ráfagas bajan más de 10 dB; el grave, menos de 0,1 dB.
- Sidechain externo: controla la banda; sin sidechain conectado no actúa; con detección interna se ignora.
- Mismo resultado bit a bit con bloques de 1, 7, 32, 100 y 4096 muestras.
- La detección sigue al canal de la banda (`tests/dsp/equalizer_channel_test.cpp`): un tono solo en el canal derecho no mueve una banda Left, ni uno centrado una banda Side.
- **Sin chasquidos:**
  - Con modulación rápida (24 dB arriba y abajo 100 veces por segundo, con el ataque mínimo), el paso máximo entre muestras no supera 1,25 veces el de la entrada.
  - Al desactivar la dinámica o mover el umbral de golpe, los agudos espurios quedan por debajo de −60 dB (medido: −77 y −82 dB). Se comprobó que los tests detectan el fallo: sin el fundido o sin el suavizado del umbral suben a −36 dB, y con la ganancia en escalones gruesos falla también el test de modulación.

## Limitaciones conocidas

- Sin ratio: la reducción sigue al exceso 1:1 hasta el rango. Ver la ADR 0006.
- Sin anticipación (lookahead): con ataques muy cortos se recorta algo de transitorio.
- Sin umbral automático.
- Coste: un 5 % de un núcleo con 24 bandas dinámicas en estéreo a 48 kHz.

## Referencias

- D. Giannoulis, M. Massberg, J. Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and Analysis*, JAES 60(6), 2012. Detector de pico con ataque y relajación, y rodilla suave.
