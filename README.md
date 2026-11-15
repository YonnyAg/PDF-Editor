# PDF Editor

Proyecto de un editor PDF de escritorio de código abierto, orientado a usuarios que necesitan trabajar con documentos mediante una interfaz clara, moderna, seria y accesible.

![Icono de PDF Editor](resources/branding/pdf-editor-64.png)

**Edición preliminar 0.1.0-preview.1 para Windows 11 x64.** Editor local en
C++20/Qt con PDF4QT, distribuido como proyecto open source bajo MIT para el código
propio. Las dependencias conservan sus licencias.

[Descargar instalador y versión portátil](https://github.com/YonnyAg/PDF-Editor/releases/tag/v0.1.0-preview.1)
· [Compilar desde fuentes](docs/DESARROLLO.md)
· [Contribuir](CONTRIBUTING.md)

El instalador no requiere administrador. El ZIP debe extraerse completo antes de
abrir `pdf_editor.exe`. Esta edición aún no tiene firma Authenticode y requiere
más pruebas en equipos limpios; no se presenta como versión estable.

**Microsoft Store:** enviado a certificación el 15 de noviembre de 2026 como
**PDF Editor Open**, gratuito, con el editor YonnyAg. Aún no está aprobado ni
disponible para descargar en la tienda. Consulta [el estado y el paquete MSIX](store/README.md).

## Visión

Crear una aplicación que permita leer, organizar, anotar y editar documentos PDF, con procesamiento local como comportamiento predeterminado. La plataforma inicial propuesta es Windows 11, con distribución futura mediante GitHub Releases y Microsoft Store. La compatibilidad con otros sistemas se evaluará después de validar la primera versión.

La experiencia debe ser comprensible para usuarios sin conocimientos técnicos: acciones con nombres claros, herramientas agrupadas por tarea y controles avanzados disponibles cuando hagan falta.

## Tecnología propuesta

La aplicación utiliza **C++20 + Qt 6 Widgets + PDF4QT 1.6.0.0**. Las revisiones del motor y las herramientas se registran en `dependencies.json`:

| Componente | Propuesta | Propósito |
| --- | --- | --- |
| Lenguaje principal | C++20 | Integración directa con el motor y Qt |
| Interfaz | Qt 6 Widgets | Ventanas, paneles, accesibilidad e interacción de escritorio |
| Motor PDF | PDF4QT 1.6.0.0 | Lectura y renderizado de páginas |
| OCR local en Windows | Windows.Media.Ocr | Reconocimiento con los idiomas instalados en el sistema |
| Firmas digitales, etapa posterior | Capacidades de PDF4QT, a validar | Firma y validación mediante certificados |
| Compilación y dependencias | CMake + MSVC + vcpkg | Construcción reproducible para Windows |
| Distribución a evaluar | MSIX | Paquete para Windows y Microsoft Store |

PDF4QT declara MIT para su código y utiliza Qt y otras dependencias con condiciones propias. Su configuración consultada utiliza C++20 y CMake. Véanse [PDF4QT](https://github.com/JakubMelka/PDF4QT), [configuración de compilación](https://github.com/JakubMelka/PDF4QT/blob/master/CMakeLists.txt) y la [comparativa técnica](docs/INVESTIGACION.md).

El código propio y el icono se distribuyen bajo [MIT](LICENSE), conservando las licencias y los avisos del motor, Qt y los demás componentes. El empaquetado utiliza bibliotecas Qt dinámicas y entrega sus avisos y fuentes correspondientes.

Python + PySide6 + PyMuPDF se conserva como alternativa si se priorizan rapidez de desarrollo y facilidad del lenguaje y se acepta la ruta AGPL. C# + WinUI 3 también se considera si la integración exclusiva con Windows pasa a ser la prioridad, después de seleccionar un motor de edición adecuado.

## Funciones previstas

El paso 27 amplía **Crear campo de formulario…** con grupos de botones de opción
y listas de selección múltiple, con relleno, historial y guardado. Añade
**Archivo → Eliminar objetos en zonas (conservar el resto)…**, que elimina objetos
completos que toquen una zona y conserva el resto del texto y gráficos en páginas
simples compatibles. Requiere fuentes estándar y ausencia de campos/anotaciones;
la copia como imágenes continúa disponible para otros documentos.

**Archivo → Lectura accesible del texto…** (`Ctrl+Alt+L`) ofrece la capa de texto
en un control estándar, con teclado, navegación, selección, copia y tamaño de
letra ajustable. Respeta permisos de copia y requiere OCR en escaneos. Consulta
el [alcance y los límites del paso 27](docs/PASO_27.md).

**Archivo → Editar contenido de esta página…** permite sustituir texto simple
con su fuente original, reemplazar imágenes y mover, escalar, girar o eliminar
objetos compatibles. Los trazados admiten cambios de color, relleno y grosor.
Se pueden modificar objetos insertados después de guardar y reabrir. Hay historial;
se rechazan páginas cuya reconstrucción no coincida con la vista previa original.
No es edición universal de párrafos y las fuentes deben contener los caracteres nuevos.

**Anotar → Formas de esta página… → Cambiar estilo…** aplica nuevos colores y
grosor a formas y dibujos libres, con apariencia persistente e historial.

**Archivo → Firmar copia con certificado…** crea una copia nueva con firma
criptográfica invisible SHA-256 usando un PFX/P12 con clave privada. Abre la copia
y usa **Verificar firmas digitales…** para consultar integridad, confianza del
certificado y cobertura por separado. Las credenciales no se guardan. No incluye
sellado de tiempo ni consultas de revocación en línea. Consulta el
[alcance de los tres bloques del paso 26](docs/PASO_26.md).

**Archivo → Eliminar contenido en una copia…** permite delimitar hasta 100 zonas
con el ratón o porcentajes, revisarlas y retirarlas de la lista antes de exportar.
Reconstruye todas las páginas como imágenes y borra los píxeles de las zonas negras.
La copia pierde búsqueda, selección, formularios, enlaces, marcadores, adjuntos y
metadatos originales. El original y los cambios pendientes se conservan. Admite
hasta 50 páginas y 150, 200 o 300 ppp, con límites de tamaño. Se rechazan documentos
cifrados, firmados, XFA o con advertencias de representación. Revisa la copia antes
de compartirla. Consulta el [alcance y los pendientes del paso 25](docs/PASO_25.md).

**Archivo → Reconocer texto (OCR)…** reconoce hasta 50 páginas por operación,
con los idiomas OCR instalados en Windows. Permite revisar el resultado, exportarlo
a un archivo TXT UTF-8 nuevo o añadir una capa invisible para buscar y copiar.
La imagen original se conserva; la incorporación admite deshacer/rehacer y guardado.
Se omiten páginas que ya tienen texto o campos interactivos. El reconocimiento
puede contener errores y debe revisarse. Cancelar descarta el resultado al terminar
la página actual. El procesamiento se realiza en el equipo.

Al abrir un PDF protegido, se solicita su contraseña, con hasta tres intentos.
**Archivo → Guardar copia con contraseña…** crea un archivo nuevo con AES-256,
contraseñas distintas de apertura y propietario y permisos de copia e impresión.
Ambas contraseñas deben tener entre 8 y 64 caracteres. La copia se vuelve a abrir
con ambas credenciales para verificarla antes de publicarla. El original y sus
cambios pendientes se conservan. Los documentos cifrados se abren en modo de
lectura y se respetan sus permisos; otros lectores pueden aplicar esos permisos
de forma diferente. Las contraseñas no se guardan en la sesión.

Las pestañas cifradas se excluyen de la recuperación automática y deben abrirse
de nuevo con contraseña. Los archivos recientes solo conservan su ruta.

Abre un PDF con `Ctrl+O` y recorre sus páginas con **Anterior** y **Siguiente** (`Ctrl+Re Pág` y `Ctrl+Av Pág`), el campo de número de página o el panel lateral de miniaturas. Escribe un número en el campo y pulsa Intro para saltar a esa página. El visor muestra una página a la vez y conserva el nivel de zoom manual al cambiar de página. Si está activado **Ajustar al ancho**, recalcula el zoom para la nueva página.

Cada documento se abre en una **pestaña independiente**, hasta un máximo de 20.
Cada pestaña conserva su página, zoom, búsqueda e historial de deshacer/rehacer.
Abrir un archivo que ya está abierto selecciona su pestaña. Puedes reordenarlas
arrastrando sus títulos, recorrerlas con `Ctrl+Tab` y `Ctrl+Mayús+Tab` y cerrar
la activa con `Ctrl+W` o su botón de cierre. Guardar afecta a la pestaña activa.
También puedes arrastrar varios archivos PDF a la ventana o pasarlos como
argumentos al ejecutable.

**Archivo → Archivos recientes** conserva hasta 20 rutas y permite vaciar la
lista sin cerrar documentos. Al iniciar, la aplicación ofrece restaurar la
sesión anterior: pestañas, orden, página, zoom, ajuste al ancho y cambios pendientes
que se alcanzaron a guardar en las copias locales. Cancelar conserva esa sesión
sin sobrescribirla. El historial de deshacer no se conserva entre ejecuciones.

La **recuperación automática** trabaja en segundo plano después de un breve
periodo sin cambios. El estado aparece junto a las pestañas. Las copias se
guardan en el perfil local del usuario; **Archivo → Abrir carpeta de recuperación**
permite localizarlas. No sustituyen el guardado manual ni garantizan recuperar
la última edición si la aplicación o el equipo se detiene antes de completar
la copia. Si el original cambió o falta, puedes conservar lo recuperado con
**Guardar como…**. Las copias dañadas o que no se pudieron abrir se conservan.

**Editar → Buscar texto…** (`Ctrl+F`) busca en la capa de texto del PDF en segundo plano. Escribe el término y pulsa Intro. El visor resalta la coincidencia activa y la lleva a la vista. Usa `F3` y `Mayús+F3` para recorrer los resultados y Esc para cerrar la búsqueda. Los documentos escaneados requieren una capa de texto para poder buscar.

**Seleccionar texto** permite arrastrar un área rectangular sobre la página para elegir un fragmento de su capa de texto. **Copiar texto seleccionado** o `Ctrl+C` con el visor enfocado lo lleva al portapapeles. Esc limpia la selección. La selección funciona sobre la página activa, respeta los permisos de copia y se descarta al cambiar de página o documento.

El botón **Resaltar selección** y su flecha permiten resaltar, subrayar, tachar o aplicar ondulado al fragmento seleccionado, sin buscarlo primero. Estas acciones también están en **Anotar → Marcar selección** y admiten deshacer/rehacer y guardado.

La pestaña lateral **Marcadores** muestra la jerarquía existente y permite saltar a sus páginas. Admite destinos internos directos y con nombre y actualiza las páginas de destino al reordenar. Los marcadores sin destino compatible se muestran con un aviso en su descripción. La apertura de destinos externos queda pendiente.

**Marcadores → Añadir marcador de la página actual…** (`Ctrl+B`) crea una entrada al final del nivel principal. Selecciona un marcador en el panel para **Cambiar nombre**, **Cambiar página de destino** o **Eliminar marcador**. Estas acciones también están en su menú contextual. La eliminación pide confirmación y retira sus submarcadores junto con la entrada, conservando las páginas del PDF. Todas las operaciones admiten deshacer/rehacer y se guardan con la sesión.

La edición conserva la jerarquía, los grupos cerrados, los atributos y las acciones de los demás marcadores. Cambiar un destino lo sustituye por un salto a la página elegida. Se aplican las restricciones de edición de PDFs cifrados, firmados o con XFA y se rechazan árboles con enlaces inconsistentes.

Pulsa **Resaltar coincidencia** en la barra de búsqueda para convertir el resultado activo en un resaltado amarillo permanente. Se guarda junto con las demás ediciones y también aparece al imprimir.

La flecha junto a **Resaltar coincidencia** permite elegir **Subrayar coincidencia**, **Tachar coincidencia** o **Subrayado ondulado**. Las marcas siguen la orientación del texto y se conservan al guardar.

**Anotar → Marcas de texto de esta página…** muestra los resaltados, subrayados, tachados y ondulados compatibles. Selecciona uno para verlo sobre la página y usa **Eliminar marca** para retirarlo tras confirmar. La eliminación se puede deshacer.

El visor incluye desplazamiento, zoom manual del 10 al 400 %, tamaño 100 % (`Ctrl+0`) y **Ajustar al ancho**. La lectura y el renderizado se realizan en segundo plano. Las miniaturas se generan a medida que aparecen en el panel. Cierra el documento con `Ctrl+W`. Cancelar el selector o fallar al leer otro archivo conserva el documento actual. Los PDFs que requieren contraseña solicitan la clave sin cerrar el documento anterior si se cancela.

La página y su miniatura muestran los iconos de notas, los resaltados y la apariencia guardada de campos AcroForm, como texto y casillas. El visor muestra un aviso para campos visibles sin apariencia guardada o formularios XFA y conserva el contenido de la página. La fidelidad de otros tipos de anotación y documentos complejos requiere más pruebas.

**Anotar → Añadir nota…** permite colocar una nota adhesiva con un clic. Pulsa Intro para colocarla en el centro de la parte visible de la página o Esc para cancelar. En **Anotar → Notas de esta página…** puedes leer las notas y editar el texto de las que sean modificables. Las notas bloqueadas o con texto enriquecido se abren en modo de lectura. Los cambios se guardan junto con las demás ediciones mediante **Guardar** o **Guardar como…**.

En el diálogo de una nota editable, **Eliminar nota** permite retirarla tras confirmar. Puedes recuperarla con **Deshacer**.

**Formularios → Rellenar campos de esta página…** permite modificar texto, marcar o desmarcar casillas y elegir opciones en listas desplegables AcroForm compatibles. Se conservan los valores de exportación cuando son distintos de las etiquetas visibles. El texto se dibuja con una fuente del sistema. Se respetan los campos de solo lectura y los límites de longitud. Los cambios admiten deshacer/rehacer y se guardan con la sesión.

**Formularios → Crear campo de formulario…** añade un campo de texto, una casilla o una lista desplegable al área porcentual indicada. El nombre debe ser único; los desplegables admiten hasta 500 opciones distintas y el documento, hasta 1000 campos para esta operación. Los campos nuevos se pueden rellenar y volver a modificar después de guardar. También se crean grupos de opción y listas de selección múltiple compatibles. El diseño posterior de campos existentes sigue pendiente.

**Anotar → Añadir forma…** crea rectángulos, elipses, líneas o flechas, con color, grosor de 0,5 a 20 puntos y comentario. Los rectángulos y las elipses admiten relleno. El área se delimita en el visor. **Formas de esta página…** permite localizarlas y eliminar las que sean editables. Se respetan bloqueos y respuestas, y se admiten deshacer/rehacer y guardado. Cambiar estilo permite aplicar colores, relleno compatible y grosor a las formas existentes.

**Anotar → Dibujar a mano alzada…** abre una vista previa de la página para trazar
con el ratón. Permite elegir color y grosor, deshacer el último trazo y borrar el
dibujo antes de insertarlo. Admite hasta 32 trazos y 10000 puntos, dentro de la
página. Todo el dibujo se añade como una sola edición y puede guardarse,
deshacerse o eliminarse desde Formas de esta página. Cambiar trazos individuales
después de guardarlos queda pendiente.

**Archivo → Adjuntos del documento…** permite incorporar archivos de hasta 64 MB, exportarlos a destinos nuevos o retirarlos de la lista. Los archivos se exportan sin abrirse. Se conservan los demás nombres internos del PDF. La gestión actual cubre archivos integrados en la lista del documento; las asociaciones AF y los adjuntos utilizados en anotaciones tienen restricciones de retirada. Retirar un adjunto no es una herramienta de supresión de datos sensibles.

**Archivo → Guardar copia optimizada…** elimina objetos sin uso y recomprime flujos compatibles sin reducir la calidad de las imágenes. Escribe un PDF nuevo, conserva el documento activo y sus cambios pendientes y no sobrescribe destinos existentes. El tamaño final puede aumentar; no se aplica reducción de resolución ni compresión con pérdida.

Para limitar la memoria, cada imagen se restringe a 8192 píxeles por dimensión y 16 millones de píxeles. Reduce el zoom si una página supera ese límite.

Selecciona una página y elige **Páginas → Girar 90° a la derecha**. El visor y la miniatura muestran el giro mientras la barra de estado indica **Cambios pendientes**. Dos giros de 90° se acumulan en 180° en la misma sesión. Esta operación admite PDFs sin cifrado, firmas digitales ni formularios XFA.

**Páginas → Mover página arriba** o **Mover página abajo** desplaza la página seleccionada una posición. El visor, el contador y el panel de miniaturas siguen la página movida. Puedes combinar movimientos y giros en una misma sesión. Las restricciones de edición se aplican a ambas operaciones.

**Páginas → Duplicar página** inserta una copia después de la seleccionada, con notas y resaltados independientes. **Insertar página en blanco después** crea una página del mismo tamaño visible. **Eliminar página…** pide confirmación y conserva al menos una página en el documento. Las tres acciones admiten deshacer/rehacer y quedan pendientes hasta guardar.

La duplicación y eliminación de páginas con campos de formulario están pendientes. Cambiar el número de páginas en PDFs etiquetados, con numeración personalizada o artículos también requiere soporte adicional. La eliminación actualiza los marcadores que apuntaban directamente a la página y se detiene si quedan referencias desde otras partes del documento.

**Páginas → Insertar páginas de otro PDF…** permite elegir un intervalo e incorporarlo antes de la primera página, después de la página activa o al final. **Unir varios PDF al final…** permite ordenar los archivos con los botones Subir/Bajar o arrastrándolos antes de incorporarlos. Ambas acciones copian contenido y anotaciones de página, conservan las ediciones pendientes y admiten deshacer como una sola operación.

Los enlaces internos del origen se ajustan a las páginas importadas. Si apuntan a una página excluida, se pide ampliar el intervalo. La importación de formularios, PDFs etiquetados, capas y perfiles de salida requiere soporte adicional. El alcance de esta primera versión se detalla en la guía de desarrollo.

**Archivo → Guardar** (`Ctrl+S`) aplica todos los cambios al documento activo. **Archivo → Guardar como…** los escribe juntos en una copia con un nombre nuevo y abre esa copia en la misma pestaña. Al cerrar un documento con cambios pendientes, puedes guardarlos, descartarlos o cancelar. Abrir otro documento conserva los anteriores en sus pestañas. Al salir se consulta cada documento pendiente; si cancelas, las pestañas y sus ediciones se mantienen. Si el archivo abierto cambió en disco, puedes conservar tus ediciones con **Guardar como…**.

**Páginas → Extraer páginas…** guarda un intervalo continuo en un PDF nuevo. El intervalo empieza en la página activa y refleja el orden y los giros de la sesión. El documento original sigue abierto con sus cambios pendientes. La extracción requiere un destino libre y admite PDFs sin cifrado, firmas digitales ni formularios XFA. Si una referencia enlaza con una página excluida, la aplicación pide ampliar el intervalo.

**Páginas → Recortar páginas…** permite ajustar los cuatro márgenes como porcentaje del área visible actual. La vista previa muestra la primera página del intervalo y delimita en azul el área que se conservará. Puedes aplicar el mismo porcentaje a una página o a un intervalo, aunque las páginas tengan tamaños o giros distintos.

**Páginas → Restaurar área completa…** vuelve a mostrar el tamaño completo definido por cada página (MediaBox), incluido el contenido oculto por recortes anteriores. Recortar solo cambia el área visible y **no elimina el contenido del PDF**. Ambas acciones admiten deshacer/rehacer y se guardan con las demás ediciones. Se mantienen las restricciones para documentos cifrados, firmados o con XFA.

**Páginas → Dividir PDF en varios archivos…** crea partes con el número de páginas que indiques. Elige una carpeta de salida nueva. Se preparan y verifican todas las partes antes de publicar esa carpeta; si una parte falla, no se publica una salida incompleta. El documento abierto conserva sus ediciones pendientes. Se aplican los mismos límites de compatibilidad que a la extracción, incluidos los enlaces que apuntan fuera de una parte. El máximo es 1000 archivos.

**Insertar → Insertar texto nuevo…** permite elegir fuente instalada, tamaño de 4 a 144 puntos, color, negrita, cursiva, subrayado y alineación. El texto admite saltos de línea y se guarda como contenido buscable dentro de un área porcentual de la página visible. Si no cabe, se pide ampliar el área o reducir la letra. **Insertar imagen…** incorpora PNG o JPEG conservando su proporción y transparencia cuando existe. Se admiten imágenes de hasta 64 MB, 8192 píxeles por lado y 16 millones de píxeles.

La posición y el tamaño de ambas inserciones se indican en porcentajes; el área aparece delimitada en el visor. Se respetan el giro y el recorte de la página. Las inserciones admiten deshacer/rehacer y guardado. La edición posterior de objetos compatibles está disponible desde Archivo → Editar contenido de esta página…, con los límites de fuentes y reconstrucción descritos.

**Insertar → Numerar páginas…** añade números visibles a un intervalo de hasta 500 páginas como una sola edición. El formato admite `{n}` para el número secuencial y `{total}` para el número de páginas del intervalo, con número inicial configurable. Puedes elegir pie de página, encabezado o un área propia. No cambia las etiquetas internas del PDF.

**Insertar → Marca de agua o sello de texto…** añade texto personalizado o uno de los modelos CONFIDENCIAL, BORRADOR, APROBADO y COPIA. Permite elegir estilos, opacidad del 1 al 100 %, giro y posición, delante o detrás del contenido existente. Los fondos opacos pueden ocultar una marca colocada detrás. Si el texto no cabe en alguna página, se rechaza la operación completa. La numeración y las marcas admiten deshacer/rehacer y guardado.

**Archivo → Propiedades del documento…** muestra el archivo y sus páginas y permite editar título, autor, asunto y palabras clave. Se actualizan tanto Info como XMP conservando las demás propiedades. Si el XMP existente está dañado o no es compatible, la actualización se rechaza sin modificar el documento. Los cambios admiten historial y se escriben al guardar.

**Insertar → Añadir enlace…** crea un área con borde azul hacia una página del mismo documento o una dirección HTTP/HTTPS. **Enlaces de esta página…** muestra sus destinos, los delimita en el visor y permite abrirlos o eliminarlos. Las páginas internas se resuelven después de reordenar. Las direcciones web se abren en el navegador tras confirmación. Los enlaces incompatibles no se ejecutan y se respetan los bloqueos de edición. Añadir y eliminar enlaces admite deshacer/rehacer y guardado.

El menú **Marcadores** también permite añadir submarcadores, mover una entrada arriba o abajo, convertirla en submarcador de la anterior o subirla un nivel. Se mueve la rama completa, conservando sus destinos, y el panel sigue la entrada movida.

**Editar → Deshacer** (`Ctrl+Z`) y **Rehacer** recorren hasta 20 ediciones de la sesión, incluidas las operaciones de páginas, anotaciones y formularios. Una edición nueva descarta los pasos que se podían rehacer. Al guardar correctamente se inicia un historial nuevo.

**Archivo → Exportar página como PNG…** guarda la página activa en un archivo nuevo a hasta 150 ppp. **Archivo → Imprimir…** (`Ctrl+P`) abre el diálogo de impresión de Windows y ajusta las páginas elegidas al papel como imágenes. Ambas acciones incluyen los cambios pendientes y respetan los permisos de copia o impresión del PDF.

Las funciones marcadas como base o posteriores están pendientes. Las etapas indican orden propuesto, no fechas de entrega.

| Área | Objetivo | Etapa |
| --- | --- | --- |
| Apertura | Abrir un archivo y leer el número de páginas | Implementado |
| Visor | Navegación entre páginas, miniaturas, desplazamiento, zoom y ajuste al ancho | Implementado |
| Anotaciones y formularios | Iconos de notas, resaltados y apariencias guardadas de campos AcroForm | Implementado |
| Herramientas de lectura | Búsqueda de texto y navegación entre coincidencias | Implementado |
| Herramientas de lectura | Selección de texto con ratón y copia al portapapeles | Implementado |
| Herramientas de lectura | Pestañas con vista e historial independientes; atajos y arrastre de archivos | Implementado |
| Archivos | Lista de recientes y restauración de pestañas, orden, página y zoom | Implementado |
| Páginas y guardado | Girar 90°, mover una posición y guardar las ediciones juntas en el archivo abierto o en una copia | Implementado |
| Archivos | Impresión y exportación de la página activa a PNG | Implementado |
| Páginas | Extraer un intervalo a otro PDF | Implementado |
| Páginas | Duplicar, eliminar e insertar páginas en blanco en PDFs compatibles | Implementado |
| Páginas | Insertar intervalos de otro PDF y unir varios archivos en el orden elegido | Implementado |
| Páginas | Recortar con vista previa, aplicar a intervalos y restaurar el área completa | Implementado |
| Páginas | Dividir un PDF en varios archivos por número de páginas | Implementado |
| Anotaciones | Crear, editar y eliminar notas | Implementado |
| Anotaciones | Resaltar, subrayar, tachar y marcar con ondulado las coincidencias; eliminar marcas | Implementado |
| Anotaciones | Marcar directamente el texto seleccionado | Implementado |
| Anotaciones | Crear rectángulos, elipses, líneas y flechas; localizar y eliminar formas | Implementado |
| Anotaciones | Dibujo a mano alzada con vista previa y eliminación | Implementado |
| Anotaciones | Cambiar colores y grosor de formas y dibujos ya guardados | Implementado |
| Anotaciones | Editar puntos individuales de trazos y mover anotaciones | Avanzada |
| Contenido nuevo | Insertar texto e imágenes en la página visible | Implementado |
| Contenido nuevo | Fuente, color, estilos y alineación del texto insertado | Implementado |
| Contenido nuevo | Numeración, sellos de texto y marcas de agua por intervalos | Implementado |
| Contenido existente | Sustituir texto simple con su fuente e imágenes compatibles | Implementado con límites |
| Contenido existente | Transformar/eliminar objetos y cambiar estilos de trazados y formas | Implementado con límites |
| Formularios | Rellenar texto, casillas y desplegables AcroForm compatibles | Implementado |
| Formularios | Crear campos de texto, casillas y desplegables | Implementado |
| Formularios | Crear y rellenar grupos de opción y listas múltiples compatibles | Implementado con límites |
| Formularios | Diseñar, cambiar tipo y eliminar campos existentes | Avanzada |
| Navegación interna | Lectura y navegación de marcadores existentes | Implementado |
| Navegación interna | Crear, renombrar, cambiar destino y eliminar marcadores | Implementado |
| Navegación interna | Crear submarcadores y mover ramas entre posiciones y niveles | Implementado |
| Navegación interna | Añadir, listar, abrir y eliminar enlaces internos y HTTP/HTTPS | Implementado |
| Protección | Apertura con contraseña y copia AES-256 con permisos de copia e impresión | Implementado |
| Protección | Eliminar zonas en una copia reconstruida como imágenes | Implementado |
| Protección | Supresión de objetos completos conservando el resto en páginas simples | Implementado con límites |
| Protección | Supresión precisa de caracteres y partes de imagen en PDFs complejos | Avanzada |
| Organización | Editar título, autor, asunto y palabras clave en Info y XMP | Implementado |
| Organización | Incorporar, listar, exportar y retirar adjuntos integrados del catálogo | Implementado |
| Organización | Copia optimizada sin reducción de imágenes | Implementado |
| Organización | Adjuntos asociados avanzados y metadatos avanzados | Avanzada |
| OCR | Capa buscable y exportación UTF-8 con idiomas instalados en Windows | Implementado |
| Firmas | Copia con firma invisible PFX/P12 SHA-256 y verificación local | Implementado con límites |
| Firmas | Sellado de tiempo, múltiples firmas nuevas y validación avanzada | Avanzada |
| Calidad de uso | Deshacer y rehacer ediciones pendientes | Implementado |
| Calidad de uso | Recuperación local de cambios pendientes en segundo plano | Implementado |
| Calidad de uso | Lectura de texto con teclado e interfaces de accesibilidad | Implementado con límites |
| Calidad de uso | Compatibilidad ampliada con tecnologías de apoyo y alto contraste | Transversal |
| Procesamiento por lotes | Aplicar operaciones compatibles a varios archivos | Posterior |
| Comparación | Comparar documentos y destacar diferencias | Investigación avanzada |
| Interoperabilidad | Investigar PDF/A, PDF/UA y conversiones con Office | Investigación independiente |

### Qué significa editar un PDF

Agregar texto, modificar un campo de formulario y sustituir el texto original de una página son operaciones diferentes. Un PDF no garantiza conservar párrafos editables, fuentes completas ni una estructura equivalente a Word. La edición del contenido existente necesita pruebas específicas de fuentes, posiciones y conservación del diseño.

No se promete edición universal como en un procesador de textos, conversión perfecta entre PDF y Office, compatibilidad completa con XFA ni certificación PDF/A o PDF/UA. Cada capacidad se incorporará cuando se haya implementado y validado.

## Diseño de la experiencia

- Barra superior con Archivo, Inicio, Editar, Anotar, Páginas, Formularios y Proteger, incorporadas según estén disponibles.
- Miniaturas y marcadores a la izquierda; documento en el centro; propiedades de la selección a la derecha.
- Paleta neutra, un color de acento discreto, tipografía legible e iconos acompañados de texto en las acciones principales.
- Temas claro y oscuro, navegación por teclado, foco visible y compatibilidad con alto contraste.
- Confirmación de cambios destructivos, previsualización y conservación del original al guardar una copia.
- Operaciones largas con progreso y cancelación; documentos grandes sin bloquear la interfaz.

## Desarrollo y contribuciones

El paso 28 prepara **PDF Editor 0.1.0-preview.1** con icono, versión, licencia MIT,
instalador EXE por usuario y ZIP portátil para Windows 11 x64. Consulta
[el alcance de esta edición](docs/PASO_28.md) y [la guía de distribución](docs/DESPLIEGUE.md).
Los artefactos y paquetes de fuentes están en `build/releases/0.1.0-preview.1`.
El ejecutable de desarrollo está en `build/paso28/Release/pdf_editor.exe`.

Pasaron **34 suites (17 Debug y 17 Release) y 35 flujos nativos de interfaz**.
La distribución pasó apertura de PDF, reinstalación, asociación opcional y
desinstalación, con 355 archivos comprobados por SHA256. Se conservaron documentos,
sesiones y el lector PDF predeterminado. Informes: `build/paso28/qa`.
La edición es preliminar: faltan firma Authenticode y pruebas en otro equipo limpio
antes de la publicación pública. El motor conserva la protección de árboles de nombres.

La estructura incluye CMake, presets para Visual Studio 2022 x64, pruebas con Qt Test/CTest y un workflow de GitHub Actions para Debug y Release. Consulta [la guía de desarrollo](docs/DESARROLLO.md) para preparar Windows, compilar y ejecutar las pruebas. En una copia nueva, ejecuta primero `scripts/setup-dependencies.ps1` para descargar las revisiones y el SDK; la primera compilación construye las bibliotecas nativas y puede tardar varios minutos.

Para abrir la versión ya compilada desde PowerShell, en la carpeta del proyecto:

```powershell
.\scripts\run.ps1
```

`run.ps1` abre el ejecutable existente y configura la búsqueda de DLL de Qt; no ejecuta CMake ni vcpkg. Para compilar y probar los cambios, usa `scripts/build.ps1`; añade `-Run` si también quieres abrir la ventana después de las pruebas. En VS Code, `Ctrl+Shift+B` compila y prueba Debug; la tarea `PDF Editor: run (Debug)` abre únicamente la versión ya compilada. Las tareas utilizan Windows PowerShell (`powershell.exe`).

Validación local del 11 de noviembre de 2026 con **4/4 suites aprobadas en Debug y 4/4 en Release**. Los subrayados, tachados, ondulados y la gestión de marcas también pasaron las pruebas con el plugin gráfico de Windows, incluidos páginas giradas, texto inclinado, guardado, eliminación y deshacer/rehacer.

Las contribuciones iniciales pueden centrarse en revisar la selección de bibliotecas, aportar casos de uso y proponer documentos de prueba sintéticos o redistribuibles. Evita compartir documentos personales, contraseñas, certificados privados o información sensible.

El código separa el punto de entrada (`src/app`), la interfaz (`src/ui`) y el adaptador de lectura (`src/core`). La integración de PDF4QT requiere Qt 6.9 o superior; el SDK usado es Qt 6.9.3 con Base, Svg y LinguistTools. vcpkg y sus overlays fijan las dependencias nativas. Un parche documentado corrige la compilación exclusiva del núcleo de PDF4QT. Consulta los [avisos de terceros](THIRD_PARTY_NOTICES.md). El código propio y el icono se distribuyen bajo [MIT](LICENSE).

## Distribución prevista

GitHub alojará el código, la documentación y las versiones de la aplicación. Microsoft Store requerirá preparar y validar el paquete, configurar la identidad del editor, completar la ficha y superar la certificación. El formato MSIX es la ruta propuesta, pendiente de un ensayo de empaquetado.

Consulta el [plan de publicación](docs/PUBLICACION.md). La edición preliminar 0.1.0-preview.1 cuenta con instalador EXE por usuario y ZIP portátil. Consulta [el despliegue y sus comprobaciones](docs/DESPLIEGUE.md). El repositorio público es [YonnyAg/PDF-Editor](https://github.com/YonnyAg/PDF-Editor); no hay ficha de Microsoft Store publicada.

## Documentación

- [Investigación de tecnologías, bibliotecas, licencias y proyectos de referencia](docs/INVESTIGACION.md)
- [Preparación para GitHub y Microsoft Store](docs/PUBLICACION.md)
- [Entorno, estructura, compilación y pruebas](docs/DESARROLLO.md)
- [Componentes de terceros y parche de integración](THIRD_PARTY_NOTICES.md)

Última revisión documental del 15 de noviembre de 2026.






