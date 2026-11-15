# Hito de avance aproximado al 80 %

El paso 23 cierra el hito de **aproximadamente un 80 % funcional** del alcance
actual, tras el bloque del paso 22 y la corrección del fallo detectado en el
lector. Se pondera la complejidad pendiente; no es una medición exacta de horas,
cobertura de pruebas ni preparación para publicar.

## Cierre del hito en el paso 23

- Motor recompilado con protección en los árboles de nombres: ciclos, nodos
  repetidos, claves duplicadas, pares incompletos y límites de profundidad,
  nodos y entradas. El caso que cerraba el proceso ahora se rechaza.
- Pruebas aisladas en subprocesos y control negativo con la biblioteca anterior,
  para verificar que se reproduce y se detecta el fallo original.
- Dibujo a mano alzada sobre una vista previa de la página, con color, grosor,
  deshacer trazos, borrar, guardado, historial y eliminación desde Formas.

## Incorporado desde el hito anterior

- Creación de campos AcroForm de texto, casillas y desplegables, con áreas y
  nombres configurables, historial y posibilidad de rellenarlos tras guardar.
- Rellenado de desplegables compatibles, conservando etiquetas y valores de
  exportación distintos. Se conservan los campos y recursos originales.
- Rectángulos, elipses, líneas y flechas con colores, grosor, relleno cuando
  corresponde y comentario; listado, localización, eliminación e historial.
- Adjuntos integrados en el catálogo: incorporar, listar, exportar a archivos
  nuevos y retirar de la lista, sin ejecutar los archivos.
- Copia optimizada que elimina objetos no usados y recomprime flujos Flate,
  conservando imágenes, formularios, anotaciones y cambios de la sesión.

La optimización no garantiza un archivo menor. Retirar adjuntos o recortar no
equivale a eliminar datos sensibles. La creación de campos no incluye todavía
un diseñador de campos existentes, radio ni selección múltiple. El cambio
posterior de estilos de las formas y de trazos ya guardados queda pendiente.

## Bloques importantes pendientes

- Edición de texto e imágenes existentes y modificación posterior de objetos
  insertados, con conservación del diseño y de las fuentes.
- OCR; contraseñas y cifrado; supresión real de contenido y firmas digitales.
- Formularios y asociaciones de adjuntos avanzados, edición de trazos, selección
  entre páginas, accesibilidad y compatibilidad ampliada.
- Endurecimiento y pruebas adicionales ante otros PDFs malformados. La corrección
  de árboles de nombres resuelve el fallo reproducido, no constituye una auditoría
  completa del motor ni una garantía general frente a archivos hostiles.
- Instalador, publicación, decisiones de licencia y validación más amplia
  con documentos reales, archivos grandes y distintos equipos.

Las conversiones de Office y la interoperabilidad avanzada siguen en investigación.
