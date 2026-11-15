# PDF Editor Open

## Descripción

Trabaja con tus documentos PDF en tu equipo, con una aplicación gratuita y de
código abierto. PDF Editor Open permite leer, buscar y seleccionar texto;
organizar, combinar y extraer páginas; añadir notas, marcas, texto e imágenes;
y rellenar formularios compatibles.

También incluye OCR local con los idiomas instalados en Windows, copias protegidas
por contraseña, recuperación de sesión, edición de objetos compatibles y una
vista de lectura del texto con teclado. Ofrece firma invisible PFX/P12 y comprobación
local, con límites. No necesita una cuenta para editar ni incorpora publicidad.

Esta primera edición es preliminar. La edición de contenido existente depende de
las fuentes y la estructura del PDF. La supresión elimina objetos completos o
reconstruye una copia como imágenes, según la modalidad y compatibilidad. No se
prometen edición universal de PDFs, PDF/UA, conversión a Office ni validación
avanzada de firmas. Conserva los originales y revisa las copias guardadas.

La interfaz está en español. Requiere Windows 11 de 64 bits. El código propio es
MIT y las bibliotecas mantienen sus licencias; el código fuente y la documentación
se encuentran en https://github.com/YonnyAg/PDF-Editor.

## Características

- Lectura, búsqueda, selección y copia de texto con respeto a los permisos del PDF.
- Organización, combinación, extracción y exportación de páginas.
- Notas, marcas, texto, imágenes y formularios compatibles.
- OCR local, copias protegidas y herramientas de supresión con límites.
- Pestañas, archivos recientes y recuperación local de cambios pendientes.
- Gratuito, sin anuncios y con código fuente disponible bajo MIT para el código propio.

## Notas de versión

Primera edición preliminar para Microsoft Store. Incluye las funciones de
0.1.0-preview.1 de GitHub en un paquete MSIX x64 con identidad propia de Store.
El número del paquete Store es 1.0.0.0.

## Notas para certificación

Aplicación clásica C++/Qt de escritorio. Requiere runFullTrust para el motor nativo,
los diálogos de archivos, guardado, impresión y procesamiento local de PDFs.
No usa elevación, controladores ni servicios propios. No requiere credenciales.
Para probar, abrir un PDF sintético, navegar, buscar, anotar y guardar una copia.
El OCR utiliza únicamente idiomas que estén instalados en Windows. Los certificados
de firma son archivos propios elegidos por el usuario; no se incluyen certificados
privados de prueba. La supresión y edición de contenido rechazan documentos
incompatibles con mensajes explicativos. Ver README para los límites detallados.
