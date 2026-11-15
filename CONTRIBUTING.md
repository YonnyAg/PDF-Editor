# Contribuir a PDF Editor

Gracias por contribuir. Esta primera edición pública es preliminar y se orienta a
Windows 11 x64. Consulta README.md para las funciones y limitaciones actuales y
docs/DESARROLLO.md para preparar Qt, MSVC, CMake y las dependencias fijadas.

Para informar un fallo, abre una incidencia con la versión, Windows, los pasos
para reproducirlo y el resultado esperado y obtenido. Utiliza PDFs sintéticos o
que tengas permiso de compartir. No adjuntes documentos personales, contraseñas,
certificados privados, registros con información sensible ni claves.

Para cambios de código, crea una rama, limita cada pull request a un problema y
explica el cambio y cómo se comprobó. Conserva las licencias y avisos de terceros.
Las contribuciones al código propio se ofrecen bajo MIT, la licencia del proyecto.

Antes de enviar un cambio funcional, ejecuta scripts/build.ps1 en Debug y Release
y añade casos de prueba cuando el comportamiento lo requiera. No incluyas .tools,
build, archivos de recuperación ni ejecutables compilados en los commits. Los
binarios se distribuyen mediante Releases junto con avisos, fuentes y SHA256.

Los cambios en dependencias, edición de contenido, contraseñas, supresión o firmas
deben explicar sus límites y comprobar que preservan el original y sus permisos.
Para problemas de seguridad, consulta SECURITY.md antes de publicar detalles.
