# Hito de avance aproximado al 70 %

El paso 21 sitúa el avance orientativo del alcance actual en **65–70 %**.
Se pondera la complejidad pendiente. El porcentaje no mide horas consumidas
ni cobertura de pruebas y no equivale a estar listo para publicación.

## Incorporado desde el hito anterior

- Hasta 20 documentos en pestañas con visores, búsquedas, cambios pendientes e
  historiales independientes. Reordenación y atajos entre pestañas, guardado de
  la activa y cierre con Guardar/Descartar/Cancelar.
- Recuperación local automática de cambios pendientes: copias verificadas en
  segundo plano, registro atómico, caché para evitar reescrituras sin cambios,
  indicador de estado y acceso a la carpeta de recuperación.
- Restauración de la sesión: archivos, orden de pestañas, pestaña activa,
  página, zoom y ajuste al ancho. Se conservan las copias que no pueden abrirse
  y se evita sobrescribir un original que cambió desde la edición recuperada.
- Lista de recientes con vaciado, apertura por arrastre de varios PDFs y por
  argumentos del ejecutable. Abrir un documento existente selecciona su pestaña.

Al cancelar la salida se conservan las pestañas y sus ediciones, incluso si
antes se había elegido descartar otro documento. Las decisiones de Guardar ya
completadas conservan su efecto. Una salida normal recuerda los documentos,
pero no restaura cambios expresamente descartados.

La recuperación conserva la última copia completada; puede perder ediciones
posteriores a esa copia. No sustituye Guardar y no persiste el historial de
deshacer, selecciones ni resultados de búsqueda. Los datos se guardan en el
perfil local del usuario y pueden localizarse desde Archivo. Solo una instancia
puede escribir la misma sesión; las demás permiten editar y guardar manualmente.

## Bloques importantes pendientes

- Edición posterior de texto e imágenes existentes y de objetos insertados.
- Selección entre páginas, accesibilidad y compatibilidad ampliada.
- Más tipos de formulario, creación de campos, formas y dibujo libre.
- OCR, contraseñas y cifrado, supresión real de datos sensibles y firmas.
- Adjuntos, optimización y metadatos avanzados.
- Instalador, publicación, decisiones de licencia y validación con más
  documentos reales, archivos grandes, equipos y escalas de pantalla.

Las conversiones de Office y la interoperabilidad avanzada siguen en investigación.
