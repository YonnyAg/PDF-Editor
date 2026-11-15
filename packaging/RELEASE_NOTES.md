# PDF Editor 0.1.0-preview.1

Primera edición pública preliminar de PDF Editor, un editor PDF local y open
source para Windows 11 x64. El código propio y el icono están bajo MIT; Qt,
PDF4QT y los demás componentes conservan sus licencias y avisos.

Incluye lectura, búsqueda y selección, organización y combinación de páginas,
anotaciones, inserción de texto e imágenes, formularios compatibles, OCR local,
copias protegidas, supresión con límites, edición compatible de objetos,
firma invisible PFX/P12 y comprobación local, pestañas y recuperación de sesión.
Consulta README.md y docs/PASO_27.md para el alcance exacto de cada función.

## Descargas

- **windows-x64-setup.exe**: instalador para el usuario actual, sin administrador.
- **windows-x64-portable.zip**: extraer completo y ejecutar pdf_editor.exe.
- **source.zip**: código propio, pruebas, iconos, documentación y scripts.
- **dependency-sources.zip**: fuentes Qt y PDF4QT con los parches utilizados.
- **SHA256SUMS.txt**: sumas SHA256 de los cuatro archivos anteriores.

El ZIP portátil también guarda la sesión en los datos locales de Windows.
`--profile "directorio"` permite usar un perfil independiente. Los idiomas OCR
se instalan mediante Windows. La asociación «Abrir con» es opcional y no cambia
automáticamente el lector predeterminado. Desinstalar conserva los documentos
y datos locales del usuario.

## Comprobaciones y límites

Pasaron 34 suites (17 Debug y 17 Release), 35 flujos nativos de interfaz y pruebas
locales del ZIP, instalación, reinstalación, asociación opcional y desinstalación.
Se verificaron 355 archivos de distribución y la integridad de los ZIP.

**Los binarios no llevan firma Authenticode.** Windows puede mostrar una advertencia
de editor desconocido. Faltan pruebas en otros equipos limpios; esta edición no
se declara estable ni certificada por Microsoft Store. La edición de contenido,
formularios, supresión, accesibilidad y firmas tienen límites documentados; no
se promete edición universal de PDFs, PDF/UA ni validación avanzada de firmas.
Conserva los originales y revisa las copias antes de compartirlas.

Las menciones a «todavía no publicado» en documentos históricos y textos del
paquete describen el estado previo a esta primera publicación.
