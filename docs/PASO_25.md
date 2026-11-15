# Paso 25: eliminación de contenido en una copia

Este bloque continúa el plan posterior al hito aproximado del 90 %. Incorpora
eliminación de zonas mediante una reconstrucción completa del documento a partir
de sus píxeles visibles. No se considera terminado el proyecto ni se asigna un
nuevo porcentaje exacto por esta única incorporación.

## Uso

En Archivo, elige **Eliminar contenido en una copia…**. Selecciona una página,
arrastra sobre la vista previa o ajusta izquierda, arriba, ancho y alto como
porcentajes del área visible. Pulsa **Añadir zona negra**. Puedes añadir varias
zonas en distintas páginas y quitar las que selecciones en la lista. La línea
azul indica la candidata; solo las zonas negras añadidas se eliminan.

Elige 150, 200 o 300 ppp y confirma que has revisado las zonas. La aplicación
solicita un destino nuevo y conserva el documento activo, sus cambios pendientes
y su historial. Tras exportar, abre y revisa la copia antes de compartirla.

## Alcance y efectos

- Todas las páginas se convierten en imágenes, incluidas las que no tienen zonas.
  Se conserva el tamaño físico y la orientación visible del recorte de cada página.
- Los píxeles de cada zona se sustituyen por negro antes de incorporarlos al PDF.
  Se amplía la zona dos píxeles hacia cada lado para cubrir sus bordes.
- No se copian objetos originales: texto, imágenes originales con píxeles ocultos,
  campos, anotaciones, enlaces, marcadores, adjuntos ni metadatos del documento.
  Las apariencias visibles que queden fuera de las zonas forman parte de la imagen.
- La copia pierde búsqueda y selección de texto, formularios editables y contenido
  interactivo. La resolución elegida limita su calidad; la codificación de las
  imágenes evita compresión JPEG adicional.
- El original conserva todo su contenido. Esta herramienta no elimina copias
  anteriores del archivo ni puede detectar información sensible que no selecciones.

Se admiten hasta 50 páginas, 100 zonas, 16 millones de píxeles por página y
100 millones en total. Se rechazan documentos cifrados, firmados, con XFA o con
advertencias de representación. Los límites se comprueban antes de renderizar.
Un error o una cancelación del núcleo elimina el temporal sin publicar destino.
La interfaz permite cancelar durante la preparación; durante la exportación
permanece ocupada hasta finalizar el trabajo en segundo plano.

## Pendientes del proyecto

Continúan pendientes la edición de texto e imágenes existentes, edición posterior
de objetos, firmas y validación criptográfica con certificados, formularios
avanzados, accesibilidad ampliada, instalador, licencia definitiva y publicación.
La eliminación que conserva contenido vectorial editable fuera de las zonas
también queda pendiente; la solución actual reconstruye toda la copia.

Versión: `build/paso25/Release/pdf_editor.exe`. Informes: `build/paso25/qa`.

Validación final: 26 conjuntos de pruebas y 28 flujos nativos de Windows desde D,
sin fallos. Se verificaron 112 archivos por SHA256 y se revisó la captura del
diálogo. Los ejecutables habituales de Debug y Release también se actualizaron.
