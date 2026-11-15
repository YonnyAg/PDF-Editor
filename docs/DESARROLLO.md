# Desarrollo paso a paso

## Paso 27. Formularios, supresión por objetos y accesibilidad

Se incorporan RadioGroup y MultiSelectList a la descripción y creación de campos.
Los grupos conservan un campo padre, Kids con Parent, estados Off/on y Opt, con
apariencias Unicode generadas por Qt. El adaptador normaliza los hijos que el
parser presenta como campos combinados, presenta el grupo una sola vez y cambia
V/AS de forma coherente. No omite Parent para acomodar el parser. Se rechazan
estados ambiguos, unísono, bloqueos y grupos vinculados con el mismo nombre.

Las listas almacenan V como matriz de valores de exportación e I como índices
ordenados. Se valida selección, ausencia de duplicados y correspondencia de
opciones vinculadas. Se actualiza TI para mostrar la primera opción seleccionada
y se regenera una apariencia con filas y resaltados. Ambas funciones conservan
historial y valores tras reabrir.

localizedRedactedCopy reutiliza el procesador de objetos, con reconstrucción de
control y recursos restringidos. Retira por completo los objetos que intersectan
las zonas y añade máscaras negras. Parte de un constructor vacío y copia solo
Contents y Resources regenerados; nunca copia la página ni Parent. Las fuentes
se reconstruyen con diccionarios mínimos de Type1 estándar y las imágenes se
decodifican y recodifican. Se rechazan anotaciones/campos, recursos extendidos,
capas y fuentes no estándar. El tamaño visible conserva CropBox, giro y UserUnit.
Antes de escribir se verifica ausencia de texto en las zonas. La modalidad raster
anterior continúa disponible para documentos que no superen estas comprobaciones.

AccessibleReaderDialog extrae la capa de texto mediante QtConcurrent, con control
de página activa para descartar resultados antiguos. Usa QPlainTextEdit de solo
lectura con nombre/descripción accesibles, tamaño ajustable, selección, copia y
atajos. No se habilita sin permiso de copia. El visor anuncia número de página,
total y zoom. No se declara accesibilidad completa ni conformidad PDF/UA.

RemainingFeaturesTest cubre radio/listas, guardado, valores inválidos, cuatro giros,
supresión y conservación de texto en otras páginas, recursos regenerados y rechazo
de documentos incompatibles. RemainingFeaturesWindowTest cubre historial, diálogos,
interfaces QAccessible, selección/copia por teclado, navegación, tamaño de texto,
permisos de copia y exportación localizada. Se repiten con el backend Windows.

## Paso 26. Edición por objetos y firmas criptográficas

PDFPageContentEditorProcessor obtiene bloques Text, Image y Path con sus estados
y transformaciones. El adaptador recoge recursos de formularios anidados y
rechaza nombres ambiguos, capas opcionales y estructuras etiquetadas. Limita a
2000 elementos y 1000 recursos, con profundidad 32. Antes de editar se reconstruye
la página sin cambios y se exige igualdad de la representación hasta 1200 píxeles
por lado, sin advertencias. No equivale a demostrar fidelidad universal.

Los textos originales se escriben con sus CID para conservar caracteres y
espaciado sin recodificación. Para sustituciones simples se buscan los caracteres
en la fuente original; las fuentes estándar tienen repertorio conocido y los
subconjuntos necesitan glifos disponibles. Se recuperan las matrices completas
desde los estados: el serializador del motor usaba Td relativo y perdía escalas
y reflejos de matrices con traslación. El adaptador emite Tm absoluto, comprobado
con texto insertado guardado y reabierto. No se cambia el motor binario.

Las transformaciones operan en coordenadas de la página visible y vuelven a
coordenadas nativas, conservando CropBox, giro, origen y UserUnit. Imágenes nuevas
se componen sobre blanco y se ajustan a su caja. Los trazados cambian colores y
grosor; las anotaciones compatibles regeneran su apariencia con el constructor,
sin cambiar sus puntos. Todas estas ediciones crean una entrada del historial.

La firma usa PDFSignatureFactory con PFX/P12 en memoria y una reserva de 32768
bytes. Prepara cuatro rangos de longitud fija, calcula la firma separada SHA-256,
rellena Contents y reabre para comprobar integridad, cobertura y número de páginas.
Conserva Fields y los recursos existentes al añadir el campo invisible. Publica
un archivo nuevo mediante temporal y renombrado. No escribe el certificado ni la
contraseña al perfil. No se firman documentos cifrados, XFA o ya firmados.

La verificación comprueba el hash del archivo guardado, limita documentos a
100 MB, firmas a 100 y campos a 10000 con profundidad 64; detecta ciclos y rangos
inválidos antes del verificador del motor. Informa por separado integridad,
confianza/vigencia, algoritmos y cobertura. No hay OCSP ni sellado de tiempo.

ContentSignatureTest cubre edición original e insertada, cuatro giros, recursos,
reapertura, estilos y firmas reales. VerifyDetachedSignature.ps1 verifica de forma
independiente la cobertura y firma SHA-256 con Windows SignedCms, sin confiar en
el certificado sintético. Se prueba contraseña incorrecta, alteración del archivo,
árbol cíclico y rangos fuera de límites. La prueba de interfaz cubre historial,
guardado, estilo de dibujo y copia firmada con informe separado.

## Paso 25. Copia con zonas eliminadas mediante reconstrucción

redactedCopy valida hasta 100 rectángulos normalizados sobre las páginas visibles,
un documento de hasta 50 páginas y 150, 200 o 300 ppp. Comprueba antes de asignar
imágenes los límites de 16 millones de píxeles por página y 100 millones por lote.
Aplica las restricciones de documentos cifrados, firmados y XFA y rechaza cualquier
página con advertencias de representación.

Renderiza cada página completa visible y sustituye los píxeles de las zonas por
negro, con redondeo hacia fuera y un margen de dos píxeles. Un QPdfWriter nuevo
recibe exclusivamente estas imágenes RGB, con LosslessImageRendering. No se
copian objetos del documento fuente ni texto, adjuntos o metadatos originales.
Cada página usa su tamaño físico visible, incorporando giro, CropBox y UserUnit.
La salida pierde contenido interactivo y vectorial; no es edición localizada de
los flujos originales ni elimina el archivo fuente.

Se escribe un temporal en el directorio de destino. Antes de publicar se vuelve
a abrir y se comprueban número y tamaño de páginas, ausencia de texto, campos,
adjuntos y marcadores. El renombrado rechaza un destino existente. Las salidas
parciales se descartan ante fallo o cancelación del núcleo. La interfaz prepara
zonas con ratón o porcentajes, permite quitarlas y exige revisar los efectos antes
de elegir destino. El trabajo de exportación utiliza la cola de salida en segundo
plano y conserva documento, historial y cambios pendientes.

RedactionTest verifica cuatro giros con origen desplazado, recorte y UserUnit=2,
borrado de píxeles, ausencia de elementos originales, páginas mixtas, presupuestos,
restricciones, destino existente y cancelación. RedactionWindowTest cubre selección
con ratón, validación, lista, revisión, cancelación y exportación con cambios pendientes.
Los resultados finales están en build/paso25/qa/comprobacion-en-D.

## Paso 24. Contraseñas, cifrado y OCR local

PdfDocument admite una contraseña por intento de apertura. La interfaz solicita
hasta tres intentos y conserva el documento anterior al cancelar. No se persisten
las credenciales. Los documentos cifrados quedan en lectura; se respetan permisos
de copia e impresión y se excluyen de SessionStore y de la cola de recuperación.

protectedCopy utiliza PDFSecurityHandlerFactory del motor para AES-256, con
contraseñas distintas de apertura y propietario. Valida longitud, confirmación
en la interfaz y destino nuevo. Escribe en un temporal del directorio de destino
y comprueba rechazo sin contraseña, autorización de usuario y propietario,
permisos y páginas antes de publicar mediante renombrado. No implementa un
algoritmo criptográfico propio ni modifica el documento activo.

OcrEngine usa Windows.Media.Ocr mediante C++/WinRT y windowsapp del SDK. Un hilo
MTA persistente mantiene el contexto WinRT y serializa las llamadas; se limpia
la caché de fábricas antes de finalizar el contexto. Las pruebas detectaron que
crear y destruir un contexto por llamada dejaba una fábrica inválida tras listar
idiomas: el ciclo de vida persistente corrige ese cierre del proceso.

El OCR se ejecuta desde QtConcurrent, hasta 50 páginas y 50000 palabras por lote,
10000 por página, con imagen renderizada a un máximo de 150 dpi y 2000 píxeles
por lado. Cada reconocimiento tiene un límite de espera de 30 segundos. Se omiten
páginas con texto o campos interactivos y se puede descartar el trabajo al terminar
la página actual. Los idiomas se enumeran del sistema; este equipo dispone de
en-US y es-MX. No se descarga ni envía el documento a un servicio externo.

La capa usa cajas normalizadas, recursos de fuente separados y modo de texto PDF
invisible (Tr=3). Se conservan las transformaciones de CropBox, giro y UserUnit.
El ajuste horizontal usa la transformación del pintor, sin estirar la fuente:
el backend nativo de Windows generaba separaciones entre letras que impedían
buscar palabras completas. La prueba nativa detecta esta regresión y verifica
búsqueda antes y después de guardar.
La inclinación se aplica alrededor del centro conforme a la documentación de
[TextAngle de Windows](https://learn.microsoft.com/en-us/uwp/api/windows.media.ocr.ocrresult.textangle?view=winrt-26100).
El resultado admite búsqueda, historial y guardado. La exportación TXT usa UTF-8
y un destino nuevo sin incorporar la capa al PDF.

SecurityOcrTest valida cifrado, permisos, rechazo de destinos existentes,
exclusión de sesión, geometría en cuatro giros, origen desplazado y UserUnit=2,
igualdad de píxeles antes y después, búsqueda y reapertura, además de OCR real.
SecurityOcrWindowTest cubre diálogos de protección, contraseña incorrecta y
cancelación, recuperación excluida, exportación TXT sin editar y capa con historial.
Los informes definitivos y manifiestos están en build/paso24/qa.

## Paso 23. Corrección del lector y dibujo a mano alzada

El parche pdf4qt-name-tree-limits.patch protege PDFNameTreeLoader antes de que
PDFDocument termine de construir su catálogo. Detecta ciclos y nodos repetidos
por identidad de diccionario, limita la profundidad a 64 y el recorrido a
100000 nodos y 100000 entradas por árbol, y rechaza claves duplicadas o pares
incompletos. Lanza PDFException para que el lector devuelva un error, evitando
la recursión ilimitada del caso detectado. Cubre todos los usuarios del cargador
en el motor, incluidos catálogo, archivos multimedia y árbol de estructura.

setup-dependencies.ps1 aplica el parche de forma repetible. La integración CMake
exige su presencia al compilar el núcleo. Este bloque recompila el motor completo
en ambas configuraciones con las dependencias ya instaladas, manteniendo los
objetos temporales en C y las fuentes activas en D. La guía está en
build/paso23/validacion/README.md. El formato y la interfaz binaria no cambian.

ReaderSafetyTest ejecuta los archivos de prueba en subprocesos con tiempo límite.
Se prueban ciclos directos e indirectos y nodos repetidos en diez categorías del
catálogo, el límite de profundidad, el exceso de nodos y entradas y los pares
incompletos. Un control negativo con la DLL anterior reproduce CrashExit en un
subproceso; la DLL corregida rechaza ese archivo sin cerrar el proceso de prueba.
La corrección cubre esta clase de fallos; no sustituye una auditoría del lector.

El diálogo de dibujo libre genera la vista previa en segundo plano. Los trazos
se capturan en coordenadas normalizadas y se muestran con color y grosor físico.
Permite deshacer el último trazo y borrar antes de confirmar. Se admiten hasta
32 trazos y 10000 puntos; un trazo que excede el límite se descarta con un aviso.
El adaptador valida puntos finitos dentro del área visible y los convierte a
coordenadas originales respetando CropBox, giro, origen y UserUnit.

Los trazos se guardan como una anotación Ink con apariencia persistente y como
una sola edición del historial. El listado de Formas permite localizarlos y
eliminarlos con las mismas comprobaciones de bloqueo y respuestas. No modifica
anotaciones existentes ni convierte el dibujo en una firma digital. Modificar
trazos individuales después de insertarlos queda pendiente.

## Paso 22. Formularios, formas, adjuntos y optimización

La creación de campos admite texto, casillas y desplegables AcroForm. Se rechazan
nombres duplicados, puntos en el nombre, áreas inválidas, campos menores de 8
puntos por lado y más de 1000 campos para la creación. Los desplegables admiten
hasta 500 opciones distintas de 256 caracteres. Se preservan Fields, DR y otras
entradas existentes. Cada campo incluye su widget y apariencia; los recursos
de fuente nuevos tienen nombres propios sin reemplazar fuentes anteriores.

Los desplegables de selección simple no editables se rellenan desde el mismo
diálogo de formularios. Se muestran las etiquetas y se guardan los valores de
exportación. Se valida la opción en todos los widgets vinculados y se generan
apariencias persistentes. Radio, listas múltiples, diseño posterior y estilos
avanzados de campos siguen pendientes. La posición de creación respeta recorte,
giro, origen y UserUnit; el texto del widget sigue las coordenadas de la página.

Las formas se implementan como anotaciones Square, Circle y Line. Las flechas
usan terminación ClosedArrow. Las áreas porcentuales se convierten a coordenadas
originales, y el grosor físico se ajusta con UserUnit. Las apariencias conservan
contenido y recursos anteriores; se materializa Length si el generador lo omite.
El listado omite anotaciones ocultas y permite eliminar formas desbloqueadas;
la eliminación mantiene las comprobaciones de respuestas y ventanas emergentes.
Todas las ediciones de campos y formas se integran con historial y guardado.

Los adjuntos se incorporan al árbol Names/EmbeddedFiles como Filespec con EF,
nombre Unicode y tamaño. Se valida el árbol y se conserva el resto de Names.
La edición reconstruye una lista ordenada conservando los Filespec originales;
admite hasta 1000 entradas y 64 MB por archivo nuevo. La exportación lee únicamente
flujos integrados, no resuelve archivos externos y publica mediante renombrado
a un destino libre. No ejecuta el adjunto ni sobrescribe archivos. La retirada
actual solo modifica la lista; se rechazan asociaciones AF del catálogo y los
archivos referenciados por anotaciones. No constituye eliminación segura de datos.

La optimización usa RemoveUnusedObjects y RecompressFlateStreams. Conserva versión,
referencias y geometría de páginas, no mezcla objetos idénticos ni reduce imágenes.
Se guarda una copia nueva con el verificador existente. La operación conserva
el documento activo y las ediciones pendientes; un archivo pequeño puede aumentar.
Se mantienen las restricciones de edición para cifrado, firmas y XFA.

Se detectó una limitación del lector PDF4QT fijado: su cargador de árboles de nombres
recorre Kids sin controlar ciclos antes de que el adaptador reciba el documento.
Un PDF con esos ciclos puede provocar un fallo del proceso. El adaptador valida
los árboles al editarlos, pero no puede sustituir esa protección previa del lector.
Ese caso se corrigió en el paso 23 mediante el parche y la recompilación del
motor. El endurecimiento adicional ante otros PDFs malformados sigue pendiente.

## Paso 21. Pestañas, recuperación y sesiones

WorkspaceWindow contiene hasta 20 editores MainWindow independientes. Cada uno
mantiene su documento inmutable, historial, visor, búsqueda y operaciones
asíncronas. Sus menús y atajos actúan sobre la pestaña visible. Los archivos ya
abiertos se identifican por ruta canónica, respetando el tratamiento de mayúsculas
de Windows. Las pestañas pueden moverse y recorrerse con Ctrl+Tab/Ctrl+Mayús+Tab.
La apertura por menú se dirige al contenedor; se conserva MainWindow aislado
para sus pruebas y para no mezclar historiales de documentos.

El contenedor admite arrastre de varios archivos locales PDF y argumentos de
línea de comandos. La lista de recientes conserva hasta 20 rutas; vaciarla no
cierra documentos ni elimina copias de recuperación. Los cambios de Guardar como
actualizan la ruta de la misma pestaña. Cerrar una pestaña consulta sus cambios.
Salir primero recoge todas las decisiones y solo cierra cuando se han resuelto;
cancelar conserva las pestañas. Un fallo de guardado deja la ventana editable.

SessionStore mantiene un registro JSON versionado y copias PDF con nombres UUID.
QLockFile evita escritores simultáneos en la misma carpeta. Se limita el registro
a 1 MB y 20 documentos, se validan rutas, identificadores, nombres de copias y
estados de vista, y se rechazan enlaces simbólicos de los archivos leídos.
Un registro inválido se conserva sin sobrescribir. La carpeta normal es sessions
dentro de QStandardPaths::AppLocalDataLocation; Archivo permite abrirla.

Tras 1,2 segundos sin cambios se captura el estado y se escribe en segundo plano.
Las copias usan el guardado verificado del motor y SHA256. QSaveFile publica el
registro después de preparar las copias. Solo tras confirmar el registro se
retiran las copias anteriores conocidas que ya no se usan. Si falla la escritura,
se conserva el registro anterior y se retiran las copias nuevas de ese intento.
No se borran archivos ajenos ni copias huérfanas no referenciadas por el registro.
La caché de documentos evita regenerar el PDF al cambiar únicamente la vista.
Si llega otra edición durante la escritura, se programa una captura posterior.

Al restaurar se verifican las copias y se reconstruye el documento con la ruta
y el hash originales. Guardar no reemplaza un original distinto o inexistente;
Guardar como permite conservar la recuperación. Se restauran orden, pestaña
activa, página, zoom y ajuste al ancho, sin historial previo. Los archivos limpios
se vuelven a leer desde disco. Las entradas que no pudieron restaurarse se
conservan en futuros registros para permitir revisión o reintento. Cancelar la
oferta inicial conserva la sesión anterior sin iniciar otra sobre ella.

Una salida normal espera el registro final y recuerda los archivos sin reintroducir
las ediciones descartadas. La recuperación no garantiza conservar cambios que
todavía no se habían copiado al producirse una interrupción ni reemplaza un respaldo
externo. Solo la instancia que tiene el bloqueo escribe recuperación; las demás
conservan el guardado manual. Los informes de este bloque están en build/paso21/qa.

## Paso 20. Numeración, marcas de agua, estilos, propiedades y enlaces

El menú Insertar añade numeración visible y marcas de agua o sellos de texto.
Se aplican a un intervalo de hasta 500 páginas, en un único constructor del
documento y una sola entrada de historial. Si alguna página tiene geometría
inválida o el texto no cabe, no se publica ninguna parte de la edición.
La numeración sustituye {n} y {total}, con inicio configurable, y conserva las
etiquetas internas. Se ofrecen encabezado, pie, centro y área porcentual propia.

El texto nuevo y las decoraciones permiten fuentes instaladas, color opaco,
negrita, cursiva, subrayado, alineación izquierda/centro/derecha y 4–144 puntos
físicos. Las marcas añaden opacidad de 1–100 %, giro de −180 a 180 grados y
posición delante o detrás. El área del visor indica la caja antes del giro;
el motor comprueba también los límites del texto girado. Los fondos opacos
existentes pueden tapar una marca situada detrás. Los estilos forman parte
del contenido nuevo; modificar después objetos existentes sigue pendiente.

Las propiedades del menú Archivo editan título, autor, asunto y palabras clave,
hasta 4096 caracteres por campo. Se sincronizan Info y sus propiedades XMP,
conservando las demás entradas y los espacios de nombres ajenos. Un XMP no
analizable, sin RDF o con varios árboles RDF provoca rechazo sin cambios.
Las modificaciones quedan pendientes hasta guardar y admiten historial.

Los enlaces nuevos usan anotaciones Link con destinos GoTo a referencias de
página o URI HTTP/HTTPS sin credenciales. La transformación del área respeta
CropBox, giro, origen y UserUnit. La lista muestra enlaces visibles, delimita
su área, navega a páginas internas o pide confirmación para abrir el navegador.
No ejecuta scripts, archivos, protocolos distintos ni cadenas de acciones.
Los enlaces bloqueados no se eliminan. Se conservan anotaciones y campos de
formulario anteriores. Añadir y eliminar se integra con el historial y guardado.

Estas operaciones mantienen las restricciones de edición para PDFs cifrados,
firmados o con XFA. No se ha añadido cifrado, OCR ni firma digital en este bloque.

## Paso 19. División, organización de marcadores y contenido nuevo

La división por número de páginas prepara las partes con el motor de extracción
y verifica cada PDF. Solo después renombra la carpeta temporal a la carpeta
final, en el mismo directorio contenedor. No sobrescribe carpetas existentes.
Un fallo conserva el documento y evita publicar partes incompletas. Se mantienen
las restricciones de extracción y el límite de 1000 partes.

El panel de marcadores añade creación de hijos y movimientos de ramas completas
entre hermanos o niveles. Los enlaces y contadores del árbol se reconstruyen
conservando los diccionarios originales, y la selección sigue la nueva ruta.
Mover dentro de una rama que exceda los límites se rechaza sin aplicar cambios.

La inserción de texto e imágenes genera un formulario PDF con recursos propios
y lo añade al contenido de la página. El contenido anterior se conserva y se
envuelve para aislar su estado gráfico. Los recursos nuevos no colisionan con
fuentes o imágenes anteriores. La transformación usa el área visible, el giro,
el origen de página y UserUnit. El texto negro usa Arial con fuentes generadas
por Qt y sigue siendo buscable. Las imágenes conservan proporción y transparencia.

El cuadro de inserción delimita el área porcentual en el visor. No permite un
área fuera de la página y el motor rechaza texto que no cabe. El límite es
10000 caracteres, 4–144 puntos, e imágenes de 16 millones de píxeles, hasta
8192 por lado y 64 MB al leer archivos. Se mantienen las restricciones de edición
para PDFs cifrados, firmados o XFA. Las inserciones y los movimientos admiten
historial y guardado; la división conserva las ediciones de la sesión.

En ese paso quedaban pendientes los estilos, la numeración y los sellos de
texto, incorporados en el paso 20. Modificar objetos existentes y cambiar un
objeto insertado después de guardar siguen pendientes.

## Paso 18. Recorte visible de páginas

**Páginas → Recortar páginas…** permite ajustar los cuatro márgenes como
porcentajes del área visible actual, con vista previa de la primera página
del intervalo. La imagen se genera en segundo plano y el contorno se actualiza
al cambiar los márgenes. No se permite aplicar un recorte sin área restante,
sin márgenes o antes de completar correctamente la vista previa.

Los porcentajes se aplican a cada página del intervalo, respetando su tamaño,
rotación y UserUnit. El adaptador transforma el rectángulo visible a las
coordenadas originales y actualiza CropBox. Conserva MediaBox, contenido,
anotaciones, formularios y destinos. El cambio del intervalo se incorpora
como una sola edición y admite deshacer/rehacer y guardado.

**Restaurar área completa…** establece el área visible de las páginas elegidas
en su MediaBox, incluso si el recorte anterior era heredado de un nodo padre.
No elimina la propiedad local para evitar que reaparezca un recorte heredado.

El recorte oculta contenido, no lo elimina del PDF. No es una herramienta de
supresión de datos sensibles. Se rechazan márgenes no finitos, negativos,
intervalos inválidos y geometrías vacías. Se mantienen las restricciones de
edición para documentos cifrados, firmados o con XFA.

## Paso 17. Edición de marcadores

El menú **Marcadores** permite crear una entrada de la página actual con
Ctrl+B, cambiar el nombre y la página de destino de una entrada seleccionada,
y eliminarla junto con sus submarcadores después de confirmar. Las acciones
también están en el menú contextual del panel. Las entradas nuevas se añaden
al final del nivel principal. El cambio de destino usa un salto interno que
ajusta la página al visor.

Los cambios se preparan sobre una copia del documento y se integran con el
historial y el guardado. No modifican el contenido ni el número de páginas.
La jerarquía se identifica por su ruta de índices en la instantánea activa.
Las selecciones del panel se conservan cuando la ruta sigue existiendo.

El adaptador conserva los diccionarios originales, las acciones de otros
marcadores, los atributos adicionales y los grupos cerrados. Solo sustituye
la acción del marcador cuyo destino se cambia. Actualiza enlaces entre
hermanos y padres, First/Last y los contadores visibles. Se validan ciclos,
enlaces inconsistentes y los límites de 64 niveles y 10.000 entradas.
Los PDFs cifrados, firmados o con XFA mantienen las restricciones de edición.

Reordenar marcadores, crear submarcadores desde la interfaz y abrir destinos
externos siguen pendientes.

## Paso 16. Selección, copia, marcas directas y marcadores

El visor permite seleccionar un área rectangular de texto con el ratón sobre
la página activa. La lectura del fragmento se ejecuta en segundo plano. La geometría usa
el recorte y la rotación de la página y se mantiene al cambiar el zoom. Cambiar
de página o documento, iniciar una edición o pulsar Esc descarta la selección.
Los resultados de selecciones anteriores no se aplican a un documento nuevo.

**Copiar texto seleccionado** y Ctrl+C con el visor enfocado envían el fragmento
al portapapeles. La operación respeta el permiso CopyContent. No hay OCR ni
selección entre varias páginas. El orden del texto depende de la capa de texto
del PDF. La colocación de notas y la selección usan herramientas excluyentes.

**Anotar → Marcar selección** y el botón de la barra permiten resaltar,
subrayar, tachar y aplicar ondulado sin una búsqueda previa. Reutilizan las
anotaciones con cuadriláteros originales y se integran con historial y guardado.

La pestaña **Marcadores** muestra el árbol del documento y navega a destinos
internos directos o con nombre. La consulta resuelve las referencias de página
del documento actual después de ediciones. Las agrupaciones sin destino y los
destinos incompatibles permanecen visibles. No se ejecutan acciones externas.
Se limitan la profundidad y la cantidad de entradas al presentar el árbol.
La creación y modificación de marcadores queda pendiente.

El script de compilación actualiza la ruta de PowerShell usada por vcpkg para
evitar rutas obsoletas después de una actualización del entorno.

## Paso 15. Subrayados, tachados y gestión de marcas

La flecha del botón **Resaltar coincidencia** en la barra de búsqueda añade
**Subrayar coincidencia**, **Tachar coincidencia** y **Subrayado ondulado**.
Las cuatro opciones trabajan sobre el resultado activo de la búsqueda.

La geometría de la marca se calcula en las coordenadas originales del texto.
Se conserva su orientación en páginas giradas y texto inclinado. El resaltado
usa amarillo, el subrayado azul y el tachado y el ondulado rojo. Las marcas
contienen el texto encontrado como comentario y están marcadas para impresión.

**Anotar → Marcas de texto de esta página…** muestra las marcas compatibles,
incluidas las creadas con otros editores. Seleccionarlas muestra una vista
previa sobre la página y su comentario. **Eliminar marca** pide confirmación
y retira la anotación junto con su ventana emergente cuando existe.

Se respetan los indicadores de solo lectura y bloqueo. La eliminación de
anotaciones con respuestas queda pendiente. Crear y eliminar marcas se integra
con deshacer/rehacer y el guardado de la sesión.

## Paso 14. Importación de páginas y unión de PDF

**Páginas → Insertar páginas de otro PDF…** lee el origen en segundo plano y
permite seleccionar un intervalo y una posición. El valor inicial propone
insertar después de la página activa. También se puede insertar al principio
o al final.

**Páginas → Unir varios PDF al final…** muestra la lista de archivos elegidos
para ordenar la incorporación. Los botones Subir/Bajar y el arrastre permiten
cambiar ese orden antes de confirmar. La lectura y la preparación se ejecutan
en segundo plano. La operación se incorpora completa a la sesión cuando todos
los orígenes se han podido leer y verificar.

Ambos flujos conservan la ruta del documento activo y sus ediciones pendientes.
Deshacer retira toda la importación de una vez y Rehacer la recupera. El visor
selecciona la primera página incorporada y actualiza el contador y las miniaturas.
Los cambios se escriben con Guardar o Guardar como.

El adaptador copia el contenido y las anotaciones de las páginas elegidas,
incluidos recursos, recorte, rotación y UserUnit. Los marcadores del documento
activo conservan sus destinos. La incorporación de los marcadores del origen
queda pendiente. Los archivos de origen se leen del disco y sus páginas pasan
a formar parte de una copia independiente en la sesión.

Las páginas se separan del árbol del origen antes de copiar sus objetos.
Los destinos internos con nombre se convierten en destinos directos para
ajustar las referencias al importarlos. Si el grafo conserva referencias a
páginas excluidas, al catálogo del origen o a dependencias incompatibles,
se rechaza toda la operación. Cada origen se prepara con su propio bloqueo
antes de bloquear el destino.

Se mantienen las restricciones de edición anteriores. Las páginas importadas
con widgets de formulario, etiquetas, artículos, partes de documento, capas o
perfiles de salida requieren soporte adicional. La importación también rechaza
acciones JavaScript entre las dependencias copiadas. El destino conserva las
restricciones de cambio de número de páginas y de perfiles de salida.

## Paso 13. Duplicación, eliminación y páginas en blanco

Las tres acciones se encuentran en **Páginas** y trabajan sobre la página
seleccionada. La selección sigue a la copia o a la página en blanco insertada.
Tras eliminar, queda seleccionada la página siguiente o la anterior si se
eliminó la última. El contador y las miniaturas se actualizan con cada operación.

La copia conserva el contenido, los recursos, la geometría y la rotación.
Las anotaciones y las ventanas emergentes reciben referencias independientes,
de modo que editar o eliminar una nota de la copia conserve la nota original.
Una página en blanco toma el tamaño físico del área visible de la página activa,
incluidos el recorte, la rotación y UserUnit.

La eliminación pide confirmación y exige al menos una página restante.
Se actualizan los marcadores con destino directo a la página eliminada,
conservando los descendientes que todavía tengan otros destinos. El motor
elimina objetos sin uso y comprueba que la página retirada sea inaccesible
desde las referencias restantes antes de aceptar el cambio.

Las operaciones conservan las restricciones de edición anteriores. La
duplicación y eliminación de páginas con widgets de formulario quedan pendientes.
Los documentos con PageLabels, árbol de estructura, artículos o partes de
documento también requieren soporte adicional para cambiar su número de páginas.
Una referencia restante, por ejemplo un enlace o un destino con nombre hacia
la página eliminada, detiene la operación y conserva la sesión.

Los resultados entran en el historial de deshacer/rehacer y se escriben con el
guardado habitual. La verificación compara las referencias, la geometría y las
anotaciones de todas las páginas conservadas.

## Paso 12. Formularios y anotaciones

**Formularios → Rellenar campos de esta página…** permite elegir un campo de
texto o una casilla AcroForm. Los cambios se aplican a la sesión y se pueden
deshacer antes de guardar. Los campos de texto admiten líneas múltiples cuando
el PDF las permite y respetan la longitud máxima indicada por el formulario.
Los campos vinculados se actualizan juntos cuando sus propiedades son compatibles.

La apariencia del texto se regenera con Arial y las sustituciones de fuentes
del sistema. Conserva el tamaño y color del texto, la alineación, el fondo y el
borde declarados por el campo. El diálogo informa del uso de una fuente del
sistema antes de aplicar el cambio. Los campos de contraseña se excluyen del
selector. Los campos de solo lectura, con texto enriquecido, celdas de caracteres
o rotación propia se consultan en modo de lectura. XFA continúa pendiente.

La barra de búsqueda incluye **Resaltar coincidencia**. Convierte el resultado
activo en una anotación amarilla del PDF, visible en el visor y en las miniaturas.
El resaltado se conserva al guardar y está marcado para impresión.

En **Anotar → Notas de esta página…**, **Eliminar nota** pide confirmación y
retira la nota editable junto con su ventana emergente. Deshacer recupera ambas.
Las notas con respuestas se conservan hasta implementar la gestión del hilo.

## Paso 11. Búsqueda, salida e historial

- **Buscar texto** usa la capa de texto de PDF4QT y calcula las posiciones del
  resaltado con la transformación del visor. La búsqueda ignora mayúsculas y
  minúsculas y conserva hasta 5000 coincidencias. Si se cambia el término, se
  cierra la barra o se modifica el documento, el resultado anterior se descarta.
- **Exportar página como PNG** rasteriza la página activa con las anotaciones de
  la vista. Usa hasta 150 ppp, ajustados a los límites de memoria del visor.
  Escribe en un temporal y publica el archivo con un nombre libre.
- **Imprimir** usa Qt PrintSupport y el diálogo de impresión del sistema.
  Ajusta imágenes de hasta 300 ppp al área imprimible y respeta los permisos del
  PDF y la marca de impresión de cada anotación. El trabajo se prepara en
  segundo plano a partir de la sesión actual.
- **Deshacer y rehacer** conserva hasta 20 estados en memoria de giros,
  movimientos y notas. Al volver al estado guardado, desaparece el indicador de
  cambios pendientes. Guardar con éxito, abrir otro archivo o cerrar el
  documento reinicia el historial.

## Paso 10. Notas adhesivas

**Anotar → Añadir nota…** activa el cursor de colocación. Haz clic sobre el PDF
o pulsa Intro para elegir el centro de la parte visible de la página. Esc cancela
la colocación. Escribe el texto y confirma para ver el icono en el visor y en la
miniatura.

**Anotar → Notas de esta página…** muestra las notas de la página activa y permite
editar su texto. Las notas bloqueadas o con texto enriquecido se pueden consultar.
Los cambios quedan pendientes hasta guardar y se aplican las mismas restricciones
para PDFs cifrados, firmados o con XFA que en las demás ediciones.

La colocación invierte la transformación que el visor usa para el área recortada,
incluida la rotación de la página. PDF4QT crea la anotación de texto, su icono
y su ventana emergente para lectores compatibles. El texto de una nota existente
se actualiza sin cambiar su posición ni su apariencia.

## Paso 9. Extracción de páginas

**Páginas → Extraer páginas…** propone la página activa en los dos extremos
del intervalo. Elige las páginas inicial y final, y luego un nombre nuevo para
el PDF. La extracción incluye los giros y el orden de la sesión y se ejecuta en
segundo plano. El documento activo conserva su selección y sus cambios pendientes.

El motor materializa los atributos heredados del árbol de páginas, verifica la
geometría, la rotación y los recursos de las páginas seleccionadas, y elimina
objetos sin uso. La copia omite marcadores, hilos de artículos, acciones del
documento, etiquetas de numeración y el árbol de estructura. Si algún objeto
conserva una referencia a una página excluida, la extracción se detiene antes de
publicar el archivo. La escritura y la verificación reutilizan el flujo de
**Guardar como…**.

## Paso 8. Reordenación de páginas

**Páginas → Mover página arriba** y **Mover página abajo** desplazan la página
seleccionada una posición. La selección sigue a la página y los controles se
desactivan en el extremo correspondiente. El nuevo orden queda pendiente y
se puede guardar junto con los giros.

El motor conserva las referencias de las páginas al cambiar el orden. Si el
árbol de páginas tiene grupos anidados, prepara una lista plana y verifica que
cada página conserve su geometría, rotación y recursos antes de aceptar la edición.
El guardado vuelve a comprobar las referencias en el mismo orden.

### Flujo de edición y guardado

**Páginas → Girar 90° a la derecha** aplica la rotación a una versión en memoria.
El visor y las miniaturas renderizan esa versión. La sesión acumula ediciones,
conserva la página seleccionada y muestra **Cambios pendientes** hasta guardar.
La rotación admite PDFs sin cifrado, firmas digitales ni formularios XFA.

**Guardar** reemplaza el archivo abierto y **Guardar como…** requiere un nombre
nuevo y convierte la copia en el documento activo. Si el guardado falla, la
sesión conserva los cambios para volver a intentarlo. Al cerrar la ventana,
cerrar el documento o abrir otro PDF, se ofrecen **Guardar**, **Descartar** y
**Cancelar**. Una apertura fallida conserva el documento de la sesión.

`PdfDocument` escribe en un temporal, vuelve a leer los bytes y comprueba el
número, orden y rotaciones de las páginas. Para una copia publica el temporal con
`QTemporaryFile::rename`. Para guardar en el archivo abierto comprueba su
SHA-256 y usa `QSaveFile` para reemplazarlo con los bytes ya verificados.
[Archivos temporales de Qt](https://doc.qt.io/qt-6/qtemporaryfile.html#rename) · [Guardado de archivos en Qt](https://doc.qt.io/qt-6/qsavefile.html#commit)

La aplicación usa el núcleo de PDF4QT v1.6.0.0 para abrir documentos en solo
lectura. Archivo → Abrir PDF y Ctrl+O inician la lectura en segundo plano.
Anterior, Siguiente, el campo de página y el panel de miniaturas controlan la
misma selección. Archivo → Cerrar documento vuelve al estado inicial. Un fallo
de apertura conserva el documento y la página que se estaba mostrando.

El visor ofrece desplazamiento, zoom del 10 al 400 %, tamaño 100 % (Ctrl+0) y
ajuste al ancho. Cambiar de página vuelve al inicio de la vista y conserva el modo
de zoom. `PageView` invalida resultados pendientes si cambia la página, el
documento o el zoom. `ThumbnailView` genera las miniaturas visibles bajo demanda,
con una tarea activa y una caché de 64 entradas. Los dos componentes renderizan
en segundo plano. `PdfDocument` serializa sus renderizados por documento.

El adaptador respeta CropBox, rotación y UserUnit a 96 píxeles lógicos por pulgada,
además del zoom y el escalado de pantalla. Dibuja primero el contenido y después
las anotaciones con `PDFAnnotationManager` en modo de vista. Las notas y los
resaltados usan la representación de PDF4QT. Los campos AcroForm usan su apariencia
guardada, incluido el estado de la casilla. Campos visibles sin apariencia y XFA
generan un aviso. Los errores de la capa de anotaciones conservan el contenido
de la página. El visor y las miniaturas comparten este proceso de solo lectura.

Las transparencias y los documentos complejos requieren más pruebas. La imagen
puede alcanzar 8192 píxeles por dimensión y 16 millones de píxeles en total.
Si excede estos límites se pide reducir el zoom.

```text
PDF-Editor/
├── .github/workflows/     Compilación y pruebas en GitHub Actions
├── cmake/                Opciones compartidas del compilador
├── docs/                 Investigación, desarrollo y publicación
├── resources/            Lugar reservado para recursos con licencia documentada
├── scripts/              Detección del entorno, compilación y ejecución
├── src/
│   ├── app/              Punto de entrada de la aplicación
│   ├── core/             Adaptador de lectura y renderizado del documento
│   └── ui/               Ventana y componentes de interfaz
├── tests/                Pruebas Qt Test registradas en CTest
├── third_party/licenses/ Avisos de código de terceros
├── dependencies.json     Revisiones fijadas del motor y herramientas
├── vcpkg.json            Dependencias nativas
├── CMakeLists.txt
└── CMakePresets.json
```

## Preparar Windows

Requisitos para los presets incluidos:

1. Visual Studio 2022 o Build Tools 2022 con la carga de trabajo **Desarrollo para
   el escritorio con C++**, MSVC x64 y Windows SDK.
2. CMake 3.25 o superior y CTest. Los scripts detectan la copia instalada con
   Visual Studio aunque no esté disponible en PATH.
3. Qt 6.9 o superior para **MSVC 2022 x64**, con Qt Base, Svg y LinguistTools.
   La configuración de CI utiliza Qt 6.9.3 como referencia inicial; no es un compromiso de usar esa
   versión en la distribución final. Antes de publicar se revisarán actualizaciones
   y avisos de seguridad de Qt y sus dependencias.

4. Git para obtener las revisiones fijadas de PDF4QT y vcpkg. Python 3.9+ se usa
   únicamente si se descarga Qt mediante aqtinstall; no forma parte de la aplicación.

No mezclar un kit Qt de MinGW con el compilador MSVC de este preset.

En una copia nueva del repositorio, preparar una vez las dependencias:

```powershell
.\scripts\setup-dependencies.ps1
```

El script descarga los repositorios y Qt dentro de `.tools`, comprueba las
revisiones de `dependencies.json` y deja intactos los checkouts que tengan otra
revisión. Si ya tienes Qt 6.9+ con los módulos requeridos, usa `-SkipQt` y configura
`QT_ROOT_DIR` o pasa `-QtRoot` al compilar. CMake/vcpkg compilan las bibliotecas
nativas en el primer build; después reutilizan los resultados. Se usan los
overlays de Blend2D y AsmJit de la revisión fijada de PDF4QT.

El preset y `build.ps1` usan `VCPKG_FORCE_DOWNLOADED_BINARIES=1` para que vcpkg
reutilice las mismas herramientas auxiliares al compilar desde distintas
terminales. Esto evita reconstrucciones causadas por diferencias entre las
versiones de PowerShell disponibles en el equipo. El script restaura la variable
al terminar; no modifica la configuración global. Referencia:
[variables de entorno de vcpkg](https://learn.microsoft.com/en-us/vcpkg/users/config-environment).

Referencias: [Qt con CMake](https://doc.qt.io/qt-6.8/cmake-get-started.html),
[herramientas de Visual Studio](https://visualstudio.microsoft.com/downloads/).

### Configurar, compilar y probar

Desde PowerShell, en la raíz del proyecto:

```powershell
.\scripts\check-environment.ps1
.\scripts\build.ps1
```

El script `build.ps1` configura, compila y ejecuta CTest, deteniéndose ante el
primer error. `check-environment.ps1` comprueba la presencia de herramientas sin
modificar variables de entorno. La configuración CMake comprueba las versiones
y compatibilidad. Las rutas del equipo quedan en la caché de `build/`, excluida
de Git.

La detección de Qt utiliza, en este orden, `-QtRoot`, `QT_ROOT_DIR` y los kits
MSVC 2022 x64 instalados en `.tools/Qt`, `C:\Qt` o `D:\Qt`. Para otra ubicación:

```powershell
.\scripts\build.ps1 -QtRoot 'E:\Herramientas\Qt\6.9.3\msvc2022_64'
```

Para compilar, probar y abrir la ventana:

```powershell
.\scripts\build.ps1 -Run
```

Para abrir el ejecutable existente sin compilar ni ejecutar vcpkg:

```powershell
.\scripts\run.ps1
.\scripts\run.ps1 -Configuration Release
```

Este es el comando indicado para volver a abrir la aplicación. `build.ps1 -Run`
espera a que terminen configuración, compilación y pruebas antes de lanzarla.
Si vcpkg muestra «esperando para realizar el bloqueo del sistema de archivos»,
otro proceso de vcpkg está usando esa instalación. Evita iniciar varias
configuraciones a la vez, incluida la configuración automática del IDE.
No borres `.tools/vcpkg/.vcpkg-root`: es un archivo de vcpkg, no un bloqueo
residual que deba eliminarse. `run.ps1` no necesita adquirir ese bloqueo.

El script añade las DLL de Qt al PATH de su proceso mientras trabaja y restaura el
valor al terminar. Esto es ejecución de desarrollo, no un instalador autónomo.
El ejecutable Debug queda en `build/windows-msvc/Debug/pdf_editor.exe`. No abrirlo
directamente sin configurar antes la búsqueda de DLL. El despliegue, avisos y
MSIX se añadirán en una etapa posterior.

Para Release:

```powershell
.\scripts\build.ps1 -Configuration Release
.\scripts\build.ps1 -Configuration Release -Run
```

VS Code incluye dos tareas en `.vscode/tasks.json`: `Ctrl+Shift+B` compila y prueba
Debug; `PDF Editor: run (Debug)` solo abre el ejecutable ya compilado. Se ejecutan con
Windows PowerShell (`powershell.exe`), incluido en Windows. No es necesario abrir
el IDE de Visual Studio para trabajar.

## Qué prueba esta etapa

CTest ejecuta cuatro suites:

- `core.pdf_document`: apertura, errores de lectura, dos giros en memoria,
  reapertura tras guardar en el original o en una copia, conservación de
  anotaciones y campos, reordenación en árboles planos y anidados, destino de
  un marcador y rechazo de destinos ocupados o modificados externamente.
  Comprueba SHA-256 antes de guardar y las restricciones de edición. Verifica la
  extracción de páginas individuales y de intervalos con giros y movimientos
  pendientes, y el rechazo de referencias a páginas excluidas. Verifica la
  posición, el contenido y la persistencia de notas en una página en blanco
  con recorte y rotación, además de la conservación de las anotaciones existentes.
  Comprueba la búsqueda en páginas giradas y que la exportación PNG conserve
  la rotación y rechace destinos ocupados. Comprueba el guardado de valores y
  apariencias de texto y casillas, la herencia y los límites del campo, la
  persistencia de resaltados y la retirada de notas con sus ventanas emergentes.
  Verifica duplicación, eliminación y páginas en blanco con árboles planos y
  anidados, independencia de las notas copiadas, tamaño físico y guardado.
  Verifica importación de intervalos, enlaces con nombre, fuentes repetidas
  y la conservación del contenido y los archivos originales. Comprueba las
  marcas en las cuatro rotaciones, texto inclinado, guardado, eliminación,
  bloqueo y respuestas de anotación.
- `ui.main_window`: arranque y cierre de ventana; apertura asíncrona, cancelación,
  recuperación tras un fallo, reemplazo y cierre del documento, zoom, botones
  de navegación, salto por número, carga de miniaturas visibles y visualización
  de un resaltado en la página y su miniatura. Prueba las rotaciones acumuladas,
  el movimiento de páginas en ambas direcciones, el guardado conjunto y el
  aviso al abrir o cerrar con cambios pendientes. Comprueba que la extracción
  abra el selector en la página activa y conserve la selección y la sesión.
  Prueba la colocación de notas con ratón y teclado, la cancelación con Esc,
  la edición del texto y su persistencia al guardar. Verifica la navegación de
  resultados, el reemplazo de búsquedas, el historial de ediciones y la exportación
  desde el menú. La prueba de impresión reabre un PDF temporal para comprobar
  el intervalo y el orden de páginas. Verifica desde los menús el relleno de
  formularios, el resaltado de búsquedas y la eliminación de notas, incluido
  deshacer, rehacer y guardar. Comprueba la organización de páginas desde el
  menú, las miniaturas, el contador, la selección, la confirmación de eliminación
  y la protección de la última página. Comprueba inserción y unión desde los
  selectores de archivo, orden de la lista, cancelación, fallos de lectura,
  deshacer/rehacer y guardado. Prueba las nuevas marcas desde la búsqueda y
  su retirada desde el diálogo de gestión de la página.
- `core.page_render`: recorte y rotaciones de 0, 90, 180 y 270 grados, texto,
  imágenes y colores, UserUnit, zoom, escalado de pantalla, notas, resaltados,
  apariencia de texto y casilla AcroForm, anotaciones ocultas y avisos cuando
  falta la apariencia o hay XFA. Comprueba parámetros inválidos, límites de
  imagen y SHA-256 del archivo original.
- `ui.page_view`: zoom, ajuste al ancho y redimensionado, límites del zoom y
  descarte de resultados durante el renderizado al cambiar de página o al cerrar
  o reemplazar un documento.

Los documentos válidos se generan con QPdfWriter o con un generador PDF sintético
de objetos y referencias cruzadas en `tests/VisualPdfData.h`, en carpetas temporales. La
variante protegida se genera con el constructor de PDF4QT exclusivamente dentro
de la prueba. No se usan documentos personales ni se requiere una conexión
de red durante las pruebas. Se utiliza el plugin Qt
`offscreen` para no depender de una pantalla en la automatización. No sustituye
la revisión visual ni las pruebas de teclado y accesibilidad en Windows.

Comprobación visual realizada en Windows el 6 de noviembre de 2026:

- Iniciar el ejecutable y comprobar que el texto y los menús se leen correctamente.
- Maximizar la ventana y verificar que el contenido se adapta.
- Cerrar con Archivo → Salir.
- Capturar el visor con el plugin Qt `windows` y verificar controles legibles,
  texto, imagen y colores del PDF sintético. Comprobar también los píxeles de la
  imagen renderizada, independiente de la captura de la ventana.

Para guardar imágenes de diagnóstico de las pruebas de renderizado e interfaz,
configura `PDF_EDITOR_TEST_ARTIFACT_DIR` con una carpeta absoluta antes de
ejecutarlas. Las pruebas guardan imágenes como `rendered-page.png`,
`navigation-window.png`, `annotations-window.png` y `pending-edits-window.png`
en los casos correspondientes.
Para evaluar tipografía de la interfaz, usa el plugin
`windows`: `offscreen` puede carecer de fuentes y no representa la apariencia
nativa. Las capturas locales se guardan bajo `build/qa`, excluido de Git.

Quedan por ampliar las pruebas de tamaños pequeños, escalado de pantalla, teclado
y lectores de pantalla. El cierre desde el botón del sistema no se ha comprobado
manualmente en esta sesión.

GitHub Actions tiene preparado un trabajo para Windows con Debug y Release. Se
ejecutará cuando el repositorio se publique o se configure un remoto; todavía no
se ha ejecutado desde este directorio.

## Estado de validación local

El paso 23 recompiló desde cero el motor y pasó nueve conjuntos CTest en Debug
y Release. Desde los paquetes finales en D pasaron los dieciocho conjuntos y
veinte flujos nativos de Windows, incluido el dibujo libre. Se verificaron por
SHA256 103 archivos: runtimes, pruebas, bibliotecas de importación y símbolos
del motor. Se inspeccionó una captura nativa del diálogo de dibujo libre.

ReaderSafetyTest pasó los casos de ciclos directos e indirectos y nodos repetidos
en diez categorías de nombres, las profundidades 0, 64, 65 y 1000, el exceso de
100000 nodos/entradas y un par incompleto. Los casos se abren en procesos hijos
con límite de tiempo. El control negativo con la DLL del paso 22 detectó CrashExit
para el caso EmbeddedFiles cíclico; el motor corregido lo rechaza sin abortar.
Los hashes y resultados están en build/paso23/qa/control-negativo.json y lector.txt.

El dibujo se probó en las cuatro rotaciones con UserUnit, guardado, reapertura,
eliminación y conservación de las apariencias anteriores. La interfaz comprobó
vista previa asíncrona, trazado con ratón, deshacer/borrar trazos, cancelación,
historial y persistencia. También pasaron los flujos nativos anteriores de
formularios, formas, adjuntos, optimización, pestañas y recuperación.

El paso 22 pasó ocho conjuntos CTest en Debug y Release con Qt 6.9.3,
MSVC 2022 y PDF4QT v1.6.0.0. Desde los ejecutables finales en D pasaron los
dieciséis conjuntos y diecinueve flujos nativos de Windows: seis de las nuevas
herramientas y trece de pestañas y recuperación. Se compararon por SHA256
los 97 archivos finales y se inspeccionaron capturas de campos, formas y adjuntos.
Los registros están en build/paso22/qa.

Las pruebas del motor verifican creación y rellenado de los tres tipos de campo
en cuatro rotaciones con UserUnit, duplicados, recursos y campos anteriores,
etiquetas distintas de los valores exportados, persistencia y nuevas ediciones
tras reabrir. Las cuatro formas se prueban en las cuatro rotaciones, con guardado,
eliminación y restauración del renderizado original. Se verifican contenido binario
de adjuntos, nombres Unicode, exportación sin sobrescritura, rechazo de nombres
duplicados y árboles no válidos, conservación de destinos con nombre y retirada.

La optimización se comprueba con objetos sin uso y con contenido mixto: imagen,
notas, formularios, recursos nuevos, formas y enlaces. Se comparan renderizado,
geometría, destinos y valores, y se vuelve a rellenar un campo tras optimizar.
La interfaz verifica validaciones, historial, guardado, selectores y conservación
del documento activo y sus cambios. Los PDFs firmados y XFA rechazan las nuevas
ediciones. La limitación observada del lector con ciclos se detalla en
build/paso22/qa/limitacion-lector.md y sigue sin resolverse.

El paso 21 pasó los seis conjuntos CTest en Debug y Release con Qt 6.9.3,
MSVC 2022 y PDF4QT v1.6.0.0. Los ejecutables finales de D pasaron de nuevo los
doce conjuntos, incluidos el límite del registro y las nuevas pruebas de sesión,
y trece flujos con el plugin gráfico nativo windows. Los 91 archivos finales se
compararon mediante SHA256. Se inspeccionaron capturas con dos pestañas y con
una edición recuperada. Los informes están en build/paso21/qa.

Las pruebas verifican aislamiento de historial, vista y atajos, guardado solo de
la pestaña activa, recientes y vaciado, reordenación y pestaña activa restauradas,
apertura durante restauración, arrastre, fallo de apertura sin perder otros
documentos, cierres con Guardar/Descartar/Cancelar, cancelación de salida sin
descartar pestañas previas y fallo de guardado que mantiene la ventana editable.

La recuperación se prueba tras una destrucción sin evento de cierre, con
escrituras concurrentes a nuevas ediciones, copias corruptas, recuperación
parcial, original cambiado o inexistente, doble instancia, bloqueo, rutas de
copia inválidas, registro sobredimensionado y rechazo sin sustituir el último
registro confirmado. La salida limpia no restaura cambios descartados y cancelar
la oferta inicial conserva la sesión anterior. Los ejecutables QtTest usan el
subsistema de consola para que sus diagnósticos sean visibles en CTest y CI.

El paso 20 pasó los cuatro conjuntos CTest en Debug y Release con Qt 6.9.3,
MSVC 2022 y el motor fijado PDF4QT v1.6.0.0. Desde los ejecutables finales en
D también pasaron ocho conjuntos y cuatro flujos nativos de Windows:
numeración, estilos/marca de agua, propiedades y gestión de enlaces.

Se verificaron intervalos y números iniciales, rechazo atómico si una página
no admite el texto, texto girado con color y transparencia en cuatro rotaciones
y UserUnit, marcas detrás de fondos opacos, persistencia de Info/XMP y datos
ajenos, XMP incompatible, bloqueos, protocolos no admitidos y destinos tras
reordenar. La interfaz verifica cancelación, deshacer/rehacer y guardado.
Las pruebas sin pantalla cargan Arial regular, negrita, cursiva y negrita cursiva
del sistema para evitar la duplicación de caracteres de la negrita sintética.

Se inspeccionaron capturas nativas del diálogo de marcas y del PDF con texto
rojo estilizado y marca de agua. Los 85 archivos de las versiones finales se
verificaron por SHA256. Los informes están en build/paso20/qa y la estimación
orientativa de 55–60 % y lo pendiente se detallan en HITO_60.md.

El paso 19 se compila con Qt 6.9.3, MSVC 2022 y PDF4QT v1.6.0.0. Se verificaron
división de ediciones pendientes, rollback de carpetas cuando una parte tiene
referencias fuera de su intervalo, conservación de páginas y anotaciones,
movimientos de ramas y persistencia de la jerarquía. Las inserciones se probaron
con cuatro rotaciones, UserUnit, origen MediaBox desplazado, transparencia,
recursos de página anteriores, conservación del estado gráfico y guardado.

Qt omite BitsPerComponent en máscaras binarias permitidas por el formato PDF.
Se materializa el valor 1 en las máscaras nuevas para interoperar con el lector
del motor fijado. Las pruebas sin pantalla cargan Arial del sistema en Windows
para que la generación de contenido use fuentes reales. La automatización de
selectores usa sus controles Qt y botones, con un tiempo límite explícito.
Los cuatro conjuntos CTest pasaron en Debug y Release. Desde los ejecutables
finales de D también pasaron ocho conjuntos y tres flujos nativos de Windows.
Los ejecutables y las bibliotecas se verificaron por SHA256; los informes
están en build/paso19/qa. El avance orientativo se documenta en HITO_50.md.

El paso 18 se compiló en Debug y Release con Qt 6.9.3, MSVC 2022 y PDF4QT
v1.6.0.0. Los cuatro conjuntos CTest pasaron en ambas configuraciones. Las
pruebas del motor comprobaron recortes asimétricos con giros de 0, 90, 180 y
270 grados, UserUnit 1 y 2, geometría heredada, guardado y reapertura,
anotaciones y formularios conservados, y restauración del área completa.
También se comprobaron intervalos, límites numéricos y documentos firmados.

La prueba nativa de Windows verificó vista previa, cambio de primera página
durante su generación, rechazo de márgenes sin área, aplicación a intervalos,
cancelación, deshacer/rehacer, guardado y restauración. Se inspeccionó una
captura del diálogo. Los ejecutables y las bibliotecas locales están en
build/paso18/Debug y Release, verificados por SHA256; los informes están en
build/paso18/qa. Las fuentes permanecen en D y la compilación usa objetos
temporales para reducir escrituras en el disco USB.
Desde las versiones finales de D se repitieron los ocho conjuntos de pruebas
y el flujo nativo de recorte. Las nueve comprobaciones pasaron. Después de
comparar los archivos por SHA256 se eliminaron los temporales de compilación.

El paso 17 se compiló en Debug y Release con Qt 6.9.3, MSVC 2022 y PDF4QT
v1.6.0.0. Los cuatro conjuntos CTest pasaron en cada configuración. La prueba
nativa de Windows verificó las cuatro acciones, cancelación de eliminación,
deshacer/rehacer, guardado y reapertura, y se inspeccionó una captura del panel.
El motor comprobó marcadores anidados, texto Unicode, eliminación de ramas y
del último marcador, conservación de acciones externas y grupos cerrados,
destinos, límites de entrada y rechazo de árboles cíclicos y firmas.

La compilación directa en D perdió acceso al disco. Se completó usando
objetos temporales, con las fuentes activas en D y el motor fijado existente.
Los ejecutables y bibliotecas se copiaron a build/paso17 y se verificaron por
SHA256. Los informes están en build/paso17/qa.
Desde los ejecutables finales en D se repitieron los ocho conjuntos de
pruebas y el flujo nativo de marcadores. Las nueve comprobaciones pasaron;
después se eliminaron los temporales de compilación de C.

El 13 de noviembre de 2026 se completó la recuperación en D. Se verificaron
los 5.470 archivos de las dos carpetas temporales mediante SHA256. Desde sus
ejecutables en D pasaron cuatro conjuntos Debug, cuatro Release y la prueba
nativa de Windows. Solo entonces se eliminaron las dos carpetas de respaldo
de C. Los registros están en build/recuperacion-2026-11-12 y la ubicación
actual se describe en ESTADO_ACTUAL.md.

El paso 16 se verificó el 12 de noviembre de 2026 en una copia de trabajo en C,
con Qt 6.9.3, MSVC 2022 y las bibliotecas existentes de PDF4QT v1.6.0.0.
Los cuatro conjuntos CTest pasaron tanto en Debug como en Release. La prueba nativa con el plugin `windows`
verificó selección con ratón, Ctrl+C, creación de marca, deshacer y navegación
por marcadores con teclado. Se inspeccionó una captura de la interfaz.

Las pruebas del motor comprobaron las cuatro rotaciones y UserUnit, arrastres
en ambos sentidos y rectos, varias líneas con saltos conservados, texto
inclinado, permisos de copia, destinos con nombre y ciclos de destinos.
El visor descarta resultados pendientes al cerrar o reemplazar documentos.

La unidad USB D presentó desconexiones y errores NTFS durante la compilación.
Se respaldaron las fuentes y se prepararon ejecutables Debug y Release con sus
bibliotecas Qt en C. Cuando D volvió a estar accesible, se recuperaron las
dependencias Release y se verificó por SHA256 que las fuentes originales y el
respaldo coinciden. Las pruebas nativas de ambas versiones también pasaron con
PATH limitado a Windows y sus bibliotecas locales, sin depender de D.

| Paso 16, entorno de validación en C | Compilación | CTest |
| --- | --- | --- |
| Debug | Correcta | 4/4 aprobadas |
| Release | Correcta | 4/4 aprobadas |

Los archivos de compilación generados en D sufrieron errores de acceso y PDB
dañados. Puede ser necesario regenerar ese build antes de compilar allí.

Validación del paso 15 el 11 de noviembre de 2026 con Visual Studio Community 2022
17.14, CMake 3.31.6, Qt 6.9.3 para MSVC 2022 x64 y PDF4QT v1.6.0.0.

| Configuración | Compilación | CTest (lectura, renderizado e interfaz) |
| --- | --- | --- |
| Debug | Correcta | 4/4 aprobadas |
| Release | Correcta | 4/4 aprobadas |

La prueba de reordenación con el plugin `windows` verificó los dos sentidos de
movimiento, la selección de la miniatura y el guardado conjunto con un giro.
Se usó un PDF sintético de tres páginas con contenido y geometrías distintas.
Las pruebas de extracción y de colocación de notas con ratón y teclado también
pasaron con el plugin `windows`.
Para el paso 11, se verificaron con el plugin `windows` la búsqueda, el historial
y la exportación desde la ventana, y la salida de impresión con un dispositivo
PDF temporal. La prueba de salida valida el contenido generado antes de
depender de una impresora física o su controlador.

Para el paso 12, las pruebas del motor y de la ventana con el plugin `windows`
verificaron texto y casillas, campos con atributos heredados, guardado de
resaltados y eliminación de notas con sus ventanas emergentes.

Para el paso 13, las pruebas con el plugin `windows` comprobaron duplicación,
eliminación e inserción de páginas en blanco, selección, contador y miniaturas.
Se validaron anotaciones independientes y la conservación de las páginas al
guardar y reabrir, además de deshacer/rehacer y los límites de las operaciones.

Para el paso 14, las pruebas de inserción y unión con el plugin `windows`
comprobaron los selectores, los intervalos, el orden de archivos, cancelaciones
y fallos de lectura. Las pruebas del motor verificaron el contenido reabierto,
anotaciones independientes, destinos internos con nombre y fuentes repetidas.

Para el paso 15, las pruebas con el plugin `windows` comprobaron creación y
eliminación de marcas desde la interfaz. Se verificaron los cuatro tipos de
marca en páginas con rotaciones de 0, 90, 180 y 270 grados, texto inclinado,
guardado y reapertura, bloqueos y respuestas de anotación.

También se verificaron la sintaxis de los scripts PowerShell y la detección del
entorno. CMake está en la instalación de Visual Studio; Qt se descargó bajo
`.tools/Qt` con aqtinstall 3.3.0, cuya herramienta y dependencias se encuentran
en `.tools/python`. Estos archivos locales están excluidos de Git y no son parte
del programa distribuido. Python se utilizó para descargar Qt; la aplicación es
C++ y no requiere Python al ejecutarse.

`setup-dependencies.ps1` descarga el SDK desde los repositorios de Qt usando
[aqtinstall](https://aqtinstall.readthedocs.io/en/stable/installation.html) y
prepara PDF4QT y vcpkg según `dependencies.json`. El parche de traducciones se
guarda en `third_party/patches` y se aplica de manera repetible al checkout.
La integración CMake también coloca `config.h` generado donde el núcleo espera
encontrarlo al compilarse como subdirectorio. Los avisos están registrados en
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Siguientes incrementos

Ampliar las pruebas y el endurecimiento del lector ante otros PDFs malformados.
Ampliar formularios con radio, selección múltiple y diseño posterior; continuar
la edición de objetos y de trazos ya insertados. La selección entre páginas y la ampliación de la
accesibilidad siguen pendientes. La modificación de texto e
imágenes existentes continúa en la etapa de investigación avanzada. OCR,
seguridad, firmas y preparación de la distribución siguen en el plan.

Cada incremento actualizará este documento y el README con el comportamiento
realmente disponible y los resultados de sus pruebas.
