# Lógica del documento

`PdfDocument` encapsula la lectura y el renderizado con PDF4QT y expone la ruta,
el número de páginas y su tamaño visible sin depender de widgets. Abre el
archivo en modo de solo lectura.
`PdfOpenResult` devuelve un documento compartido o un mensaje de error.

La ventana inicia la lectura mediante Qt Concurrent y recibe el resultado con un
`QFutureWatcher`. El hilo de trabajo no accede a los widgets ni captura la ventana.
Si la lectura falla, la ventana conserva el documento anterior.

`renderPage` devuelve un `PdfRenderResult` con una QImage, error o avisos. Usa
QPainter sobre la imagen y recursos locales a cada llamada; la interfaz convierte
el resultado a QPixmap únicamente en el hilo de GUI. Respeta CropBox, rotación,
UserUnit, zoom y la proporción de píxeles del dispositivo. Limita la imagen a
8192 píxeles por dimensión y 16 millones de píxeles antes de reservar memoria.

`PageView` coordina una tarea de renderizado a la vez y descarta resultados
obsoletos al cambiar de documento, página o zoom. `ThumbnailView` solicita vistas
reducidas de las páginas visibles. `PdfDocument` serializa las llamadas a
`renderPage` por documento para proteger el acceso al motor desde ambas vistas.
El gestor de anotaciones usa la misma transformación que el contenido y dibuja
la apariencia estática de los campos AcroForm. La ausencia de apariencia de un
campo visible, la presencia de XFA y los errores al dibujar anotaciones se
comunican como avisos y conservan la página. Las transparencias y los documentos
complejos requieren ampliar la validación.

`rotatePage` y `movePage` devuelven una nueva versión en memoria y conservan la
huella SHA-256 del archivo del que partió la edición. `movePage` conserva las
referencias de las páginas y comprueba sus propiedades antes de aplicar el
nuevo orden. `save` serializa la versión activa en un temporal y vuelve a leerlo
para verificar el número, orden y rotaciones de las páginas.
La copia requiere un destino libre. El reemplazo del archivo abierto comprueba
su huella antes de confirmar el guardado. El resultado verificado se convierte
en el punto de partida de futuras ediciones. El historial queda para una etapa
posterior.
