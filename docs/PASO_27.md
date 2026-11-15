# Paso 27: formularios, supresión localizada y lectura accesible

Este bloque incorpora los tres pendientes siguientes a la edición y las firmas:
ampliación de formularios, una modalidad localizada de eliminación y mejoras de
lectura mediante teclado e interfaces de accesibilidad. Cada función tiene los
límites descritos aquí; el proyecto sigue en desarrollo.

## Formularios avanzados

**Formularios → Crear campo de formulario…** admite ahora grupos de botones de
opción y listas de selección múltiple. Los grupos contienen entre 2 y 20 opciones
distintas, distribuidas verticalmente en el área elegida; cada opción necesita
al menos 12 puntos de alto. Las listas admiten hasta 500 opciones distintas.

**Rellenar campos de esta página…** permite elegir una opción del grupo o marcar
varias entradas de una lista. En la lista, Espacio cambia la selección con el
teclado. Se respetan permisos de solo lectura y bloqueos de los widgets. Los valores
y apariencias se conservan al guardar y reabrir y admiten deshacer/rehacer.
Se conserva la separación entre etiquetas visibles y valores de exportación.

Los grupos se presentan una sola vez aunque tengan varios widgets. No se editan
grupos vinculados distintos con el mismo nombre, estados duplicados o botones
en unísono. El diseño posterior, cambio de tipo y eliminación de campos existentes
siguen pendientes. Las listas de selección simple que no son desplegables y los
desplegables editables no se incorporan en este bloque.

## Supresión localizada por objetos

**Archivo → Eliminar objetos en zonas (conservar el resto)…** ofrece selección
con ratón o porcentajes y una copia nueva. Elimina **completo** cada bloque de
texto, imagen o trazado que toque una zona, con margen conservador para bordes;
puede retirar contenido que quede fuera del rectángulo. La zona se pinta de negro.
Los objetos restantes conservan texto buscable e información vectorial.

Esta primera modalidad admite hasta 50 páginas y 100 zonas en documentos simples
con fuentes estándar. Se rechazan campos, anotaciones, capas, estructura etiquetada,
fuentes incrustadas/no estándar y recursos gráficos ambiguos o extendidos. Antes
de eliminar se compara la reconstrucción sin cambios con la vista original, hasta
1200 píxeles por lado. Si no coincide, no se publica la copia.

El archivo se construye desde un documento vacío. Se copian únicamente contenidos
y recursos regenerados: fuentes estándar con diccionarios mínimos e imágenes
decodificadas y recodificadas sobre blanco. No se copian páginas completas, catálogos,
flujos originales, adjuntos, marcadores ni metadatos originales. Se comprueba que
no quede texto seleccionable en las zonas. El original y sus cambios se conservan.

Revisa la copia antes de compartirla. No es supresión carácter por carácter ni
recorte parcial de imágenes. Para documentos incompatibles se mantiene **Eliminar
contenido en una copia…**, que convierte todas las páginas en imágenes. Ninguna
modalidad identifica por sí sola toda la información sensible del documento.

## Lectura accesible

**Archivo → Lectura accesible del texto…** (`Ctrl+Alt+L`) abre el texto de la capa
del PDF en un control estándar de solo lectura, con nombre accesible por página,
selección y copia, ajuste del tamaño de letra y navegación. `Alt+Izquierda` y
`Alt+Derecha` recorren páginas; `Ctrl+A` y `Ctrl+C` seleccionan y copian. Tab recorre
los controles y Esc cierra el diálogo. El tamaño de lectura no modifica el PDF.

La extracción se ejecuta en segundo plano y descarta resultados de otra página
cuando se cambia rápidamente. Los documentos sin permiso de copia no habilitan
esta vista. Los escaneos requieren OCR; el orden del texto depende del PDF y no
sustituye una estructura semántica etiquetada. También se mejoran los nombres
accesibles de campos y el anuncio de página y zoom del visor.

Se comprueban las interfaces de accesibilidad de Qt y la operación por teclado.
Quedan pendientes pruebas manuales amplias con Narrador/NVDA, alto contraste,
tecnologías de apoyo y documentos complejos. No se declara conformidad PDF/UA.

## Trabajo pendiente

Continúan pendientes edición avanzada de párrafos y fuentes, modificación de puntos
de trazos, formularios y firmas avanzados, supresión más precisa y compatible,
accesibilidad ampliada, instalador, licencia definitiva y publicación.

Ejecutable: `build/paso27/Release/pdf_editor.exe`. Informes: `build/paso27/qa`.

Validación final desde D: 34 conjuntos CTest y 35 flujos nativos de interfaz,
sin fallos, además de los dos núcleos nuevos con el backend Windows. Se verificaron
126 archivos por SHA256 y se revisaron las capturas de las herramientas. Los
ejecutables habituales de Debug y Release también se actualizaron.
