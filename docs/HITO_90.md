# Hito de avance aproximado al 90 %

El paso 24 alcanza **aproximadamente un 90 % funcional** del alcance actual.
Es una estimación de las funciones disponibles y de la complejidad pendiente;
no representa horas exactas, cobertura de pruebas ni preparación para publicar.

## Incorporado en este hito

- Apertura de PDFs con contraseña, reintento y cancelación conservando el documento
  anterior. Lectura con permisos de copia e impresión y herramientas de edición
  desactivadas para documentos cifrados.
- Exportación de una copia AES-256 con contraseñas de apertura y propietario,
  confirmación y permisos configurables. Verificación de apertura y permisos
  antes de publicar un destino nuevo, sin sustituir el original.
- OCR local con los idiomas instalados en Windows, selección de hasta 50 páginas,
  revisión de resultados y cancelación. En este equipo se verificó reconocimiento
  real, con inglés y español disponibles.
- Capa de texto invisible buscable, historial, guardado y exportación independiente
  del texto reconocido como UTF-8, conservando la imagen del documento.

No se guardan contraseñas ni copias descifradas en la recuperación de sesión.
Las pestañas cifradas se excluyen de la recuperación y requieren nueva apertura.
El OCR omite páginas con texto y campos, y aplica límites de resolución y palabras;
su precisión depende del documento. Es necesario revisar el resultado.

## Trabajo pendiente

- Edición localizada del texto e imágenes existentes y modificación posterior de
  objetos insertados, conservando fuentes y diseño.
- Supresión real de contenido sensible y firma/validación criptográfica mediante
  certificados. Recortar, cubrir contenido o retirar adjuntos no lo sustituye.
- Formularios avanzados, asociaciones de adjuntos, edición de trazos, selección
  entre páginas y mejora de accesibilidad y compatibilidad.
- Pruebas con más documentos reales, archivos grandes, entradas malformadas,
  distintos idiomas, otros equipos y versiones compatibles de Windows.
- Instalador, licencia definitiva, revisión de dependencias y publicación.

La corrección del lector del paso 23 sigue incorporada. No supone una auditoría
completa del motor. Las conversiones de Office y otras interoperabilidades
avanzadas continúan en investigación.

Ejecutable: `build/paso24/Release/pdf_editor.exe`. Informes y manifiestos:
`build/paso24/qa`. El porcentaje deberá revisarse si cambia el alcance.

Validación final desde D: **22 conjuntos de pruebas y 25 flujos nativos**, sin
fallos. Se verificaron 106 archivos por SHA256 y se revisaron las capturas de
los diálogos de OCR y protección. Se actualizaron los ejecutables habituales
de Debug y Release conservando el motor corregido del paso 23.
