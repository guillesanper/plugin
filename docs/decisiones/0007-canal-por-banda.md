# 0007. Canal por banda: Stereo, Left, Right, Mid o Side

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La fase 4 pide que cada banda pueda actuar sobre los dos canales, solo sobre uno, o solo sobre la suma (Mid) o la diferencia (Side). Había que decidir:

1. Cómo se codifica y decodifica Mid/Side: una vez para todo el ecualizador, o en cada banda.
2. Cómo se garantiza que la parte de la señal que una banda no procesa no cambie.
3. Qué escucha el detector de una banda dinámica que no es estéreo.
4. Qué pasa al cambiar de canal con audio sonando, y con audio mono.
5. Cómo encaja el parámetro en la tabla congelada.

## Decisión

1. **Por banda.** Las bandas siguen en serie y cada una elige su parte de la señal. Codificar una sola vez obligaría a agrupar las bandas por canal y cambiaría el orden en que suenan.
2. **Mid/Side por diferencia.** La banda calcula `m = (L+R)/2` (o `s = (L−R)/2`), la filtra, y suma a los canales solo lo que el filtro ha cambiado: `d = filtro(m) − m`, `L += d`, `R += d` (en Side, `R −= d`). No hay decodificación `L = M+S`, que en coma flotante no devuelve exactamente la entrada. Consecuencia: la otra componente pasa intacta, y una banda que no hace nada (campana a 0 dB) deja pasar toda la señal bit a bit, igual que en Stereo. Left y Right procesan directamente un canal.
3. **El detector escucha la misma parte de la señal que procesa la banda**, tanto con detección interna como con sidechain externo: el canal izquierdo, el derecho, la suma o la diferencia de la fuente. En Stereo la detección sigue siendo enlazada (manda el canal más fuerte). Una fuente mono (un sidechain mono, por ejemplo) se escucha tal cual.
4. **Cambiar de canal es un cambio estructural**: hace el mismo fundido de 10 ms que cambiar de tipo, entre la banda en el canal antiguo y en el nuevo. El filtro nuevo, el detector y la envolvente arrancan de cero, porque pasan a oír otra señal. **Con audio mono**, Stereo, Left y Mid procesan el único canal; Right y Side no hacen nada.
5. **Un parámetro por banda, `bNN_chan`, al final de la tabla** (después de la dinámica), con su función de índice `core::channelParameter()`. Opciones, en este orden: Stereo, Left, Right, Mid, Side. El valor por defecto es Stereo y el formato de estado no cambia de versión: los estados anteriores cargan en Stereo y suenan igual.

## Consecuencias

- Con todas las bandas en Stereo, el sonido es idéntico bit a bit al de la fase 3.
- El orden de las opciones de `chan` y los valores de `dsp::ChannelMode` son contrato (hay `static_assert` en `core/src/band_model.cpp`). Un modo nuevo se añade al final.
- Mid + Side con los mismos ajustes equivale a Stereo salvo redondeo (diferencia < 5·10⁻⁶), y Left + Right es idéntico a Stereo. Lo comprueban los tests de nulo.
- La curva blanca de la interfaz suma todas las bandas sin distinguir el canal: con bandas en canales distintos es una orientación, no la respuesta de un canal concreto. El analizador sigue mostrando la media de los canales.
- Coste: una banda Mid o Side hace dos pasadas extra muy baratas sobre el tramo (suma y reparto); filtra un solo canal, así que cuesta menos que una estéreo.
