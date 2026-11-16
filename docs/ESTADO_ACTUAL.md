# Estado al 16 de noviembre de 2026

**PDF Editor Open está aprobado y disponible gratuitamente en Microsoft Store**,
con el editor YonnyAg y Store ID 9P8Z4GWSH6H8. Se comprobó la ficha pública:
https://apps.microsoft.com/detail/9P8Z4GWSH6H8. El código, la documentación y las
descargas externas están publicados en https://github.com/YonnyAg/PDF-Editor.
La licencia del código propio sigue siendo MIT. La publicación de Store no cambia
las limitaciones funcionales ni la firma de los ejecutables externos de GitHub.
Identidad, empaquetado e historial de publicación: store/README.md.

El paso 28 prepara PDF Editor 0.1.0-preview.1 con icono, metadatos, MIT para el
código propio, instalador EXE por usuario, ZIP portátil y paquetes de fuentes.
Los artefactos y sus sumas están en build/releases/0.1.0-preview.1. La versión
actual está en build/paso28/Release y Debug. Pasaron 34 suites y 35 flujos nativos;
la prueba de distribución se registra en build/paso28/qa/distribution-complete.
Consulta PASO_28.md y DESPLIEGUE.md. Quedan pendientes la firma Authenticode de las
descargas externas y ampliar las pruebas en otros equipos limpios. La publicación
en GitHub y Store está completada; todavía no se declara edición estable.

## Antecedente: paso 27

El paso 27 añade grupos de botones de opción y listas de selección múltiple,
supresión localizada por objetos y una vista de lectura accesible. Los campos
admiten creación, relleno, historial y reapertura. La supresión localizada elimina
objetos completos que toquen las zonas, conservando el resto del texto y gráficos
en documentos simples compatibles; la modalidad raster anterior se mantiene.
La vista de lectura ofrece texto estándar, teclado, ajuste de letra e interfaces
de accesibilidad, respetando permisos de copia. Alcance y límites en PASO_27.md.

La versión actual es build\paso27\Release\pdf_editor.exe y Debug está en
build\paso27\Debug. Las fuentes siguen en D y se conserva el motor corregido
del paso 23. Informes y configuración están en build\paso27\qa y validacion.

Pasaron 34 conjuntos CTest y 35 flujos nativos de interfaz desde D, sin fallos,
además de los dos núcleos nuevos con el backend Windows. Se verificaron 126 archivos
por SHA256 y se actualizaron los ejecutables habituales de Debug y Release. El
resultado está en build\paso27\qa\comprobacion-en-D\resultado.json.
Tras volver a verificar las 126 copias en D, se eliminó el temporal de compilación
del paso 27 en C. El registro está en build\paso27\qa\limpieza-temporales.json.

## Antecedente: paso 26

El paso 26 incorpora edición de contenido existente compatible, modificación
posterior de objetos y firma/verificación criptográfica. El editor sustituye
texto simple con su fuente original, reemplaza imágenes y transforma objetos;
también modifica estilos de trazados, formas y dibujos libres. Tiene historial,
guardado y reapertura. Se rechazan páginas que no superen la reconstrucción de
control. Las funciones y límites están en PASO_26.md.

La firma crea una copia invisible SHA-256 usando un PFX/P12 con clave privada.
Se verifica la integridad antes de publicar y el informe separa confianza y
cobertura. No se persisten credenciales ni se añaden firmas a archivos firmados.
No incluye sellado de tiempo ni revocación en línea. Las pruebas contrastan la
firma con Windows SignedCms además del motor PDF.

La versión anterior es build\paso26\Release\pdf_editor.exe, con Debug en
build\paso26\Debug. Se conserva el motor corregido del paso 23. Las fuentes
siguen en D; informes y configuración están en build\paso26\qa y validacion.

Pasaron 30 conjuntos CTest y 31 flujos nativos de interfaz desde D, sin fallos,
además del núcleo nuevo con el backend Windows. Los 120 archivos finales están
verificados por SHA256 y se actualizaron los ejecutables habituales de Debug y
Release. El resultado está en build\paso26\qa\comprobacion-en-D\resultado.json.
Tras verificar de nuevo las 120 copias en D, se eliminó el temporal de compilación
del paso 26 en C. El registro está en build\paso26\qa\limpieza-temporales.json.

## Antecedente: paso 25

El paso 25 continúa el plan con eliminación de zonas en una copia reconstruida
como imágenes. Permite seleccionar con el ratón o porcentajes, añadir y quitar
zonas de varias páginas, revisar y exportar sin cambiar el original ni el historial.
La copia pierde la capa de texto y los elementos interactivos y no incorpora
objetos ni metadatos originales. Los efectos y límites se detallan en PASO_25.md.

La versión anterior es build\paso25\Release\pdf_editor.exe, con Debug en
build\paso25\Debug. El motor sigue siendo el corregido del paso 23. Los informes
están en build\paso25\qa y la configuración en build\paso25\validacion.
Las fuentes activas permanecen en D.

La validación del paso 25 pasó 26 conjuntos de pruebas y 28 flujos nativos de
Windows desde D, sin fallos. Se verificaron 112 archivos por SHA256 y se
actualizaron los ejecutables habituales de build/windows-msvc en Debug y Release.
El resultado está en build\paso25\qa\comprobacion-en-D\resultado.json.
Tras volver a verificar las 112 copias en D, se eliminó el temporal de compilación
del paso 25 en C. El registro está en build\paso25\qa\limpieza-temporales.json.

## Antecedente: paso 24

El paso 24 alcanza aproximadamente el 90 % funcional. Incorpora apertura con
contraseña, copias AES-256 con permisos, OCR local de Windows, capa invisible
buscable y exportación de texto UTF-8. Los documentos cifrados quedan en lectura
y se excluyen de las copias de recuperación. Las contraseñas no se persisten.
El alcance y los pendientes se detallan en HITO_90.md.

La versión anterior es build\paso24\Release\pdf_editor.exe; Debug está en
build\paso24\Debug. Usa el motor corregido del paso 23. Los informes y
manifiestos están en build\paso24\qa; la configuración reproducible está en
build\paso24\validacion. Las fuentes activas permanecen en D.

Pasaron 22 conjuntos de pruebas y 25 flujos nativos de Windows desde D, sin fallos.
Se verificaron 106 archivos por SHA256 y se actualizaron los ejecutables habituales
de build/windows-msvc en Debug y Release. El resultado está en
build\paso24\qa\comprobacion-en-D\resultado.json.
Tras volver a comprobar las 106 copias en D, se eliminó únicamente el temporal
de compilación del paso 24 en C. El registro está en
build\paso24\qa\limpieza-temporales.json.

El OCR reconoce hasta 50 páginas por operación con idiomas instalados en el
sistema. Se omiten páginas con texto o campos. Se verificó reconocimiento real
y conservación de los píxeles al incorporar la capa. La precisión requiere
revisión humana y pruebas adicionales con documentos reales.

## Antecedente: paso 23

El paso 23 cierra el hito aproximado del 80 % funcional. Corrige el fallo de
árboles de nombres que se había detectado en el lector y añade dibujo a mano
alzada con vista previa, color, grosor, deshacer trazos, historial y guardado.
La estimación y el trabajo restante se detallan en HITO_80.md.

La versión anterior es build\paso23\Release\pdf_editor.exe, con Debug en
build\paso23\Debug. En este paso se recompila también PDF4QT desde sus fuentes
con el parche de protección; no se usa la DLL histórica del paso 22. La guía
de compilación está en build\paso23\validacion\README.md y los informes en
build\paso23\qa.
Pasaron nueve conjuntos CTest en Debug y Release. Desde D pasaron dieciocho
conjuntos y veinte flujos nativos. Se verificaron por SHA256 103 archivos,
incluidos las bibliotecas y los símbolos del motor recompilado. El resultado
está en qa\comprobacion-en-D\resultado.json dentro de build\paso23.
Los ejecutables habituales de build/windows-msvc y sus DLL, bibliotecas de
importación y símbolos del motor también se actualizaron y verificaron.
Tras verificar las 38 comprobaciones, el control negativo y las copias de los
103 archivos, se eliminó el temporal de compilación completo del paso 23 en C,
incluida la copia aislada del motor anterior. El registro está en
build\paso23\qa\limpieza-temporales.json. Las bibliotecas de importación y
los símbolos conservados están en build\paso23\engine.

El lector ahora rechaza ciclos, nodos repetidos, claves duplicadas y pares
incompletos en árboles de nombres. Limita profundidad, nodos y entradas antes
de completar el catálogo. Las pruebas se aíslan en subprocesos; el control
negativo con la DLL anterior reproduce el fallo y la versión corregida lo rechaza.
No se presenta esta corrección como una auditoría completa de PDFs malformados.

El dibujo libre admite hasta 32 trazos y 10000 puntos dentro de la página visible,
con grosor de 0,5 a 20 puntos. Se inserta como una sola anotación Ink y se puede
eliminar desde Formas de esta página. Cambiar trazos ya guardados queda pendiente.

El paso 22 añadió creación de campos AcroForm de texto, casillas y desplegables,
rellenado de desplegables con valores de exportación, formas de anotación,
gestión de adjuntos del catálogo y exportación de una copia optimizada sin
reducir imágenes. El avance de ese paso se estimaba en 75–80 %.

La versión del paso 22 se conserva en build\paso22\Release\pdf_editor.exe y Debug está en
build\paso22\Debug. Las fuentes siguen en D. La configuración que usa el motor
fijado está en build\paso22\validacion\CMakeLists.txt; los informes y manifiestos
están en build\paso22\qa. Los ocho conjuntos CTest pasaron en Debug y Release.
Desde D pasaron dieciséis conjuntos y diecinueve flujos nativos. Se verificaron
97 archivos por SHA256 y se inspeccionaron capturas de las tres herramientas
visuales. El resultado está en qa\comprobacion-en-D\resultado.json dentro de
build\paso22.
Los ejecutables habituales de build/windows-msvc/Debug y Release también se
actualizaron y verificaron. Tras confirmar las 35 comprobaciones desde D y las
copias por SHA256, se eliminó únicamente el temporal de compilación del paso 22
en C. El registro está en build\paso22\qa\limpieza-temporales.json.

Campos, formas e incorporación/retirada de adjuntos admiten historial y guardado.
Los campos nuevos se pueden rellenar tras reabrir. Los adjuntos se exportan sin
ejecutarlos, solo a destinos libres. La copia optimizada conserva el documento
activo y sus cambios pendientes; puede aumentar el tamaño de un archivo pequeño.
No reduce la resolución de imágenes ni garantiza supresión de datos sensibles.

En ese paso se registró la limitación del motor con árboles de nombres cíclicos,
corregida en el paso 23. Siguen pendientes edición de objetos existentes, OCR,
cifrado, firmas, diseño avanzado de formularios, edición de trazos guardados,
accesibilidad, más pruebas de robustez e instalador.

El paso 21 añadió documentos en pestañas con vista e historial independientes,
recuperación automática local de cambios pendientes, restauración de sesiones,
archivos recientes, atajos y apertura por arrastre o argumentos del ejecutable.
El avance de ese paso se estimaba en 65–70 %; el detalle histórico está en HITO_70.md.

La versión del paso 21 se conserva en build\paso21\Release\pdf_editor.exe y Debug en
build\paso21\Debug. Las fuentes permanecen en D. La configuración reproducible
que usa el motor fijado está en build\paso21\validacion\CMakeLists.txt y los
informes, manifiestos y capturas están en build\paso21\qa.
Pasaron los seis conjuntos CTest en Debug y Release. Desde los ejecutables
finales de D pasaron doce conjuntos y trece flujos nativos de Windows. Se
verificaron 91 archivos por SHA256 y se inspeccionaron capturas de pestañas y
recuperación. El resultado está en qa\comprobacion-en-D\resultado.json dentro
de build\paso21.
Los ejecutables habituales de build/windows-msvc/Debug y Release también se
actualizaron y verificaron. Tras confirmar las 25 comprobaciones desde D y los
archivos copiados, se eliminó únicamente el temporal de compilación del paso 21
en C. El registro está en build\paso21\qa\limpieza-temporales.json.

Las pestañas no comparten historial ni búsqueda. Guardar afecta a la activa;
abrir otro archivo conserva los anteriores. Se admiten hasta 20 documentos.
Al cerrar, Guardar/Descartar/Cancelar se resuelve por documento. Cancelar la
salida mantiene todas las pestañas; un fallo de guardado deja la ventana editable.

Las copias de recuperación se verifican y se registran en segundo plano. No
sobrescriben el original automáticamente ni sustituyen Guardar. Se conserva
la última copia completada, no necesariamente la última edición al interrumpirse
la aplicación. El historial de deshacer no se restaura entre ejecuciones.
Los datos de sesión se guardan en el perfil local del usuario y pueden abrirse
desde Archivo → Abrir carpeta de recuperación. Son datos de la aplicación,
independientes de los temporales de compilación.

Las copias que no pueden recuperarse se conservan. Si el original cambió o
desapareció, Guardar no lo reemplaza; Guardar como permite conservar lo recuperado.
Los registros dañados no se sobrescriben y solo una instancia escribe la misma
sesión. La apertura y el guardado manual siguen disponibles en otras instancias.

El paso 20 añadió numeración por intervalos, marcas de agua y sellos de texto,
estilos del contenido nuevo, propiedades Info/XMP y enlaces internos y web.
Las ediciones admiten deshacer/rehacer y guardado. El avance de ese paso se
estimaba en 55–60 %; el detalle histórico se registra en HITO_60.md.

La versión del paso 20 se conserva en build\paso20\Release\pdf_editor.exe, con Debug en
build\paso20\Debug. Ambas incluyen las bibliotecas locales. Las fuentes
siguen en D; los objetos de compilación se generan temporalmente en C para
reducir las escrituras intensivas en el disco USB. Los ocho conjuntos CTest
pasaron: cuatro Debug y cuatro Release. Los informes están en build\paso20\qa
y la configuración reproducible en build\paso20\validacion\CMakeLists.txt.
Desde D pasaron también los ocho conjuntos y cuatro flujos nativos de Windows.
Se verificaron por SHA256 los 85 archivos finales y se inspeccionaron las
capturas del diálogo de marcas y del documento con texto estilizado y marca
de agua. El informe final está en qa\comprobacion-en-D\resultado.json dentro
de build\paso20.
Los ejecutables habituales de build/windows-msvc/Debug y Release también se
actualizaron y verificaron. Tras confirmar las doce comprobaciones y las copias
por SHA256, se eliminó únicamente el directorio temporal de compilación de este
paso en C. El registro está en build\paso20\qa\limpieza-temporales.json.

La numeración y las marcas se aplican a un máximo de 500 páginas como una
sola edición; si el texto no cabe en alguna, se rechaza el cambio completo.
Las propiedades conservan metadatos ajenos y rechazan XMP dañado. Los enlaces
admiten navegación interna o apertura de HTTP/HTTPS en el navegador tras
confirmación; no ejecutan scripts, archivos ni cadenas de acciones.

El paso 19 completó el bloque orientado al hito aproximado del 50 %:
división por número de páginas, submarcadores y movimientos de ramas,
inserción de texto nuevo buscable e inserción de PNG/JPEG con transparencia.
La estimación de ese paso era 45–50 %. El detalle histórico está en HITO_50.md.

La versión del paso 19 se conserva en build\paso19\Release\pdf_editor.exe y Debug en
build\paso19\Debug, con bibliotecas locales. Los ocho conjuntos de pruebas
desde D y los tres flujos nativos de división, marcadores y contenido nuevo
pasaron. Se inspeccionó una captura con texto e imagen insertados. Los informes
están en build\paso19\qa y la configuración en build\paso19\validacion.
Los ejecutables habituales de build/windows-msvc también corresponden al paso
19. Se verificaron los archivos copiados por SHA256 y se eliminaron los
temporales de compilación de C tras las once comprobaciones desde D.

Los movimientos e inserciones admiten deshacer/rehacer y guardado. La división
mantiene las ediciones pendientes y solo publica la carpeta final cuando todas
las partes están preparadas. La inserción añade contenido nuevo; modificar el
texto o las imágenes existentes continúa pendiente. También faltan pestañas,
recuperación, más formularios, OCR, seguridad, firmas y distribución.

El paso 18 añade recorte por márgenes porcentuales con vista previa, aplicación
a intervalos y restauración del área completa definida por MediaBox. Está en
el menú Páginas. Las acciones admiten deshacer/rehacer y guardado. El recorte
solo oculta contenido; no lo elimina del PDF.

La versión del paso 18 se conserva en build\paso18\Release\pdf_editor.exe, con bibliotecas
locales. Debug está en build\paso18\Debug. Los ejecutables habituales de
build/windows-msvc también están actualizados. Las fuentes nuevas del diálogo
están en src/ui/CropDialog.cpp y CropDialog.h; la operación está en PdfDocument.

Los cuatro conjuntos CTest pasaron en Debug y Release. Desde D también pasaron
los ocho conjuntos y el flujo nativo de recorte en Windows. Se comprobaron
rotaciones, UserUnit, recortes heredados, intervalos, contenido conservado,
vista previa, cancelación, historial, guardado y restauración. Las bibliotecas
y los ejecutables copiados se verificaron por SHA256 antes de limpiar los
temporales de compilación. No se recrearon los respaldos de fuentes de C.
Los informes y la captura están en build\paso18\qa; la configuración de
validación está en build\paso18\validacion\CMakeLists.txt y usa las fuentes
activas y el motor fijado de D.

El paso 17 añade creación de marcadores de la página actual con Ctrl+B,
cambio de nombre, cambio de página de destino y eliminación con confirmación
de la entrada y sus submarcadores. Las acciones están en el menú Marcadores
y el menú contextual del panel. Admiten deshacer/rehacer y guardado.

Se preservan las acciones y los atributos originales de otras entradas,
los grupos cerrados y la jerarquía. Se rechazan árboles con enlaces dañados,
ciclos o dimensiones fuera de los límites. Los PDFs cifrados, firmados o con
XFA mantienen las restricciones de edición. La creación de submarcadores
desde la interfaz y la reordenación, pendientes en ese paso, se incorporaron
en el paso 19.

La versión del paso 17 se conserva en build\paso17\Release\pdf_editor.exe, con bibliotecas
locales. Debug está en build\paso17\Debug. Las fuentes permanecen en src y las
pruebas nuevas en PdfDocumentTest y MainWindowTest. Los informes y la captura
nativa están en build\paso17\qa.

El paso 16 conserva selección rectangular de texto, Ctrl+C, cuatro marcas
sobre la selección y navegación por los marcadores internos.

La validación completa pasó los cuatro conjuntos CTest en Debug y Release.
También pasó la prueba nativa de Windows de selección, copia, marca, deshacer
y marcadores en ambas configuraciones.
Se comprobaron rotaciones, UserUnit, varias líneas, texto inclinado, permisos,
destinos con nombre y descarte de resultados obsoletos.

El proyecto activo está en D:\Program\PDF-Editor. Por las desconexiones
anteriores del disco USB se había creado un respaldo temporal en C.
El 13 de noviembre se verificó la copia manual en
build\recuperacion-2026-11-12, dentro del proyecto en D.

Los 5.470 archivos de ambos directorios coincidieron por SHA256, incluidas
las fuentes, dependencias, ejecutables, informes y archivos de compilación.
Desde D pasaron los cuatro conjuntos Debug, los cuatro Release y la prueba
nativa de Windows, con PATH limitado a Windows y bibliotecas locales.
Después de esas comprobaciones se eliminaron únicamente las dos carpetas
temporales pdf-editor-respaldo-2026-11-12 y pdf-editor-validacion de C.

En build\recuperacion-2026-11-12 se conservan manifest-sha256.json,
comprobacion-final-2026-11-13\resultado.json y limpieza-C.json como registros.
La instantánea Release del paso 16 está en
build\recuperacion-2026-11-12\pdf-editor-validacion\runtime-release\pdf_editor.exe.
La versión Debug está en runtime dentro del mismo directorio de validación.
Ambas incluyen sus bibliotecas Qt locales y funcionan sin el respaldo de C.

Las carpetas recuperadas conservan una instantánea de la compilación previa;
sus cachés CMake contienen rutas originales de C y deben reconfigurarse antes
de recompilar allí. Para continuar el desarrollo se usa el proyecto activo
y scripts/build.ps1. Los fallos anteriores dañaron algunos PDB generados en
build/windows-msvc, por lo que puede ser necesario regenerar ese build.

Para el paso 17, la compilación sobre D volvió a perder acceso a archivos.
Se usó una carpeta temporal para los objetos y ejecutables de compilación,
manteniendo las fuentes activas en D. Las versiones finales se copiaron a D
y sus archivos se compararon por SHA256. La configuración de validación que
usa el motor fijado existente está en build\paso17\validacion\CMakeLists.txt.
Desde D pasaron ocho conjuntos de pruebas (cuatro Debug y cuatro Release)
y el flujo nativo de marcadores en Windows. Después se eliminaron los
temporales de compilación de C. Los ejecutables habituales de
build/windows-msvc/Debug y Release también quedaron actualizados.

El plan de funcionalidades y las limitaciones están en README.md y
DESARROLLO.md. Siguen pendientes, entre otras áreas, selección entre páginas,
más tipos de formulario y edición avanzada
de contenido, OCR y distribución. La división, los submarcadores, sus movimientos
y la inserción básica de texto e imágenes se implementaron en el paso 19;
los estilos, la numeración, las marcas de agua, las propiedades y los enlaces,
en el paso 20. Las pestañas, los recientes y la recuperación se incorporaron
en el paso 21.
