# Investigación técnica

Actualización del 6 de noviembre de 2026: se adoptó C++20 + Qt Widgets y se
integró la lectura y el renderizado inicial con PDF4QT v1.6.0.0. La aplicación
muestra la primera página con zoom y ajuste al ancho. Compila con Qt 6.9.3 y
las cuatro suites de lectura, renderizado e interfaz pasan en Debug y Release. Véase la
[guía de desarrollo](DESARROLLO.md) para el estado actual y los límites.
El análisis siguiente conserva la investigación y las propuestas del 4 de noviembre.

Fecha: 4 de noviembre de 2026. Investigación documental sobre fuentes oficiales; no se han compilado ni medido los motores. Las recomendaciones son criterio de ingeniería, no resultados de un benchmark. Las licencias deben verificarse otra vez sobre las versiones exactas que se distribuyan.

## Recomendación actualizada

Después de la consulta sobre licencias, se recomienda ensayar primero **C++20 + Qt 6 Widgets + PDF4QT**, con CMake, MSVC y vcpkg para Windows. Es una recomendación pendiente de adopción: todavía no se ha construido el motor ni comprobado su API con nuestros casos de uso.

La recomendación inicial de Python priorizaba facilidad de desarrollo. Al ponderar además la amplitud de edición y una posible licencia permisiva para nuestro código, PDF4QT resulta más atractivo. El coste es un lenguaje y una compilación más complejos. No se presupone que el usuario haya elegido MIT ni que toda la distribución quede bajo esa licencia.

La configuración oficial consultada establece C++20, utiliza Qt Widgets en las aplicaciones y permite construir solo la biblioteca central. El siguiente prototipo debe comprobar si podemos integrar esa biblioteca detrás de una interfaz propia sin arrastrar todo el editor. No se recomienda crear bindings Python o mezclar C# y C++ en la primera etapa: añadiría trabajo de integración antes de validar el motor.

Fuentes: [CMake de PDF4QT](https://github.com/JakubMelka/PDF4QT/blob/master/CMakeLists.txt), [dependencias vcpkg](https://github.com/JakubMelka/PDF4QT/blob/master/vcpkg.json), [licencias de Qt](https://doc.qt.io/qt-6/licensing.html). Las secciones siguientes conservan el análisis de alternativas; los detalles de multiprocessing y pyHanko corresponden a la ruta Python, no al stack C++ propuesto.

## 1. Criterios de selección

Se priorizan código comprensible para colaboradores, amplitud de operaciones PDF, experiencia de escritorio, mantenimiento, procesamiento local y distribución en Windows. También se considera la posibilidad de extender el producto a otros sistemas, sin convertirla en un requisito inicial.

La interfaz no determina la capacidad de edición: primero hay que validar un motor PDF. Ninguna de las opciones de interfaz aporta por sí sola todas las funciones de un editor convencional.

## 2. Comparativa de tecnologías

| Ruta | Ventajas para este proyecto | Costes o límites | Valoración |
| --- | --- | --- | --- |
| Python + PySide6 + PyMuPDF | Desarrollo incremental accesible; Qt de escritorio; API amplia de PDF con motor nativo | Empaquetado con dependencias nativas; AGPL o licencia comercial; edición textual compleja requiere trabajo propio | Alternativa si se prioriza facilidad del lenguaje y se acepta AGPL |
| C# + WinUI 3 | Integración con Windows y Fluent Design; herramientas de distribución de Microsoft | WinUI se orienta a Windows; hay que seleccionar y validar por separado un motor de edición completo | Mejor alternativa si Windows nativo prima sobre portabilidad |
| C# + WPF | Ecosistema maduro de escritorio y composición flexible | Modernizar la apariencia exige diseño; persiste la necesidad de un motor PDF | Alternativa válida para productividad de escritorio |
| C++ + Qt + PDF4QT | Motor y aplicaciones existentes para estudiar o reutilizar; control de rendimiento; motor con licencia MIT | Mayor complejidad de C++, compilación e integración; revisar Qt y dependencias incluidas | Recomendación actual para el prototipo |
| TypeScript + React + Tauri / Rust | Interfaz flexible; ruta documentada a Microsoft Store | Dos ecosistemas; PDF.js y pdf-lib no cubren por sí solos toda la edición solicitada | Adecuada si se reduce alcance o se integra otro motor |
| TypeScript + Electron | Herramientas web y distribución de escritorio conocidas | Añade un runtime Chromium; tampoco resuelve el motor de edición | Menor prioridad para el alcance actual |

Fuentes: [Qt for Python](https://doc.qt.io/qtforpython-6/), [elección de tecnologías Windows](https://learn.microsoft.com/en-us/windows/apps/whats-new/personae), [WinUI y Windows App SDK](https://github.com/MicrosoftDocs/windows-dev-docs/blob/docs/hub/apps/get-started/index.md), [PDF4QT](https://github.com/JakubMelka/PDF4QT), [Tauri y Microsoft Store](https://v2.tauri.app/distribute/microsoft-store/).

No se declara que Python sea más rápido ni que C# sea incapaz de editar PDF. La recomendación combina facilidad de contribución y API disponible; el rendimiento real dependerá del motor, el renderizado y los documentos de prueba.

## 3. Bibliotecas y responsabilidades

| Biblioteca | Uso evaluado | Licencia de referencia | Límites y decisión inicial |
| --- | --- | --- | --- |
| PySide6 | Interfaz de escritorio | LGPLv3/GPLv3/comercial, según componentes | Revisar cada módulo de Qt utilizado; propuesta inicial: Qt Widgets |
| PyMuPDF / MuPDF | Renderizar, extraer y buscar texto; manipular páginas, contenido, anotaciones y widgets | AGPL / comercial | Motor de la alternativa Python; no garantiza reconstrucción automática de párrafos |
| pikepdf / QPDF | Operaciones estructurales, metadatos y cifrado | pikepdf: MPL-2.0; dependencias con licencias propias | Complemento opcional, solo si demuestra una necesidad que el motor principal no cubre |
| Tesseract | Reconocimiento de texto | Apache-2.0 | Requiere binarios y modelos de idioma; OCR no reconstruye automáticamente la maquetación original |
| pyHanko | Firmar y validar documentos con certificados | MIT | Incorporar después de estabilizar el guardado; revisar también dependencias criptográficas |
| PDF4QT | Motor C++/Qt y editor de referencia | MIT para el proyecto; terceros con sus propias licencias | Ensayar como alternativa a la ruta Python |
| PDF.js | Visualización en una interfaz web | Apache-2.0 | Biblioteca de renderizado; no sustituye un motor completo de edición |
| pdf-lib | Crear PDF, dibujar texto/imágenes, páginas y formularios en JavaScript | MIT | No ofrece API para editar/eliminar texto corriente existente; no soporta documentos cifrados |
| PDFsharp / MigraDoc | Generación y manipulación desde .NET | La rama master consultada declara AGPL o comercial | No asumir MIT a partir de tutoriales antiguos; verificar el tag exacto y su alcance funcional |

Fuentes: [licencias de Qt](https://doc.qt.io/qt-6/licensing.html), [licencias de PyMuPDF](https://pymupdf.io/licensing), [API de páginas](https://pymupdf.readthedocs.io/en/latest/page.html), [pikepdf y terceros incluidos](https://github.com/pikepdf/pikepdf/blob/main/README.md), [Tesseract](https://tesseract-ocr.github.io/tessdoc/), [pyHanko](https://docs.pyhanko.eu/en/latest/), [licencia pyHanko](https://github.com/MatthiasValvekens/pyHanko/blob/master/LICENSE), [licencia PDF.js](https://github.com/mozilla/pdf.js/blob/master/LICENSE), [funciones y límites de pdf-lib](https://github.com/Hopding/pdf-lib), [licencia actual de PDFsharp](https://github.com/empira/PDFsharp/blob/master/LICENSE).

### Decisión de licencia pendiente

Publicar un repositorio en GitHub no basta para dar permiso de reutilización. Si se adopta PyMuPDF en su modalidad libre, la propuesta es distribuir el proyecto bajo términos compatibles con AGPL y proporcionar el código fuente correspondiente, instrucciones y avisos exigibles. Elegir MIT para nuestro código no elimina las obligaciones de las dependencias.

Si se prefiere una base permisiva, conviene ensayar C++/Qt/PDF4QT y revisar todo su árbol de dependencias. Una licencia MIT en el motor tampoco convierte Qt ni sus otros componentes en MIT.

Para LGPL, hay que revisar las condiciones de enlace, modificación, sustitución de bibliotecas y entrega de avisos/código aplicables al paquete concreto. La publicación en Store y el cumplimiento de licencias son verificaciones distintas; no se ha establecido todavía la compatibilidad del paquete final con todos los términos. Referencia: [obligaciones de Qt LGPL](https://www.qt.io/development/open-source-lgpl-obligations).

## 4. Proyectos de referencia

| Proyecto | Qué estudiar | Precaución al reutilizar |
| --- | --- | --- |
| [PDF4QT](https://github.com/JakubMelka/PDF4QT) | Arquitectura de editor, anotaciones, formularios, OCR, comparación y firmas | Proyecto MIT desde 2025; revisar licencia de la revisión elegida y terceros |
| [PDF Arranger](https://github.com/pdfarranger/pdfarranger) | Interacción sencilla con miniaturas, rotación, recorte, unión y división de páginas | Python/GTK y pikepdf; GPLv3; es un organizador, no un editor universal de contenido |
| [Stirling PDF](https://github.com/Stirling-Tools/Stirling-PDF) | Organización de herramientas y amplitud de flujos de trabajo | La licencia raíz tiene excepciones por directorio; no asumir que todo el repositorio es MIT |
| [Ejemplos oficiales de PyMuPDF](https://pymupdf.readthedocs.io/en/latest/recipes-text.html) | Inserción de texto y tratamiento de fuentes y cajas | Son ejemplos técnicos; requieren adaptación a una aplicación con historial y guardado seguro |

La propuesta inicial es crear una aplicación propia y usar estos proyectos como referencias. Un fork de PDF4QT podría reducir parte del trabajo si su arquitectura e interfaz encajan; debe compararse con el coste de mantener modificaciones y actualizarse desde upstream. No se ha copiado código de estos proyectos.

Fuentes complementarias: [README PDF Arranger](https://github.com/pdfarranger/pdfarranger/blob/main/README.md), [licencia PDF Arranger](https://github.com/pdfarranger/pdfarranger/blob/main/COPYING), [licencia Stirling con excepciones](https://github.com/Stirling-Tools/Stirling-PDF/blob/main/LICENSE), [versión de escritorio de Stirling](https://docs.stirlingpdf.com/Desktop/Install/).

## 5. Límites que afectan al producto

**Texto existente.** Detectar texto y coordenadas no asegura recuperar párrafos editables. Fuentes incrustadas parcialmente, ligaduras, columnas, rotaciones y texto convertido a curvas dificultan la edición. Un reemplazo localizado mediante eliminación e inserción puede cambiar tipografía y espaciado. Debe ofrecer previsualización y no anunciarse como reflujo universal. Véanse [inserción de texto](https://pymupdf.readthedocs.io/en/latest/recipes-text.html) y [operaciones de página y eliminación](https://pymupdf.readthedocs.io/en/latest/page.html).

**Información sensible.** Un rectángulo opaco o recortar la vista no elimina necesariamente lo que hay debajo. La función de eliminación permanente deberá aplicar la operación del motor, guardar sin conservar revisiones recuperables y comprobar texto, imágenes, metadatos y adjuntos según el alcance anunciado. Nunca presentar un simple resaltado negro como eliminación segura.

**Firmas.** Una imagen manuscrita no es una firma digital criptográfica. Al editar un documento firmado se pueden invalidar sus firmas o cambiar su estado de validación. Firmar debe ser una operación final separada; se requiere validar certificados, confianza y cambios permitidos. pikepdf declara que no soporta firmas digitales: [seguridad de pikepdf](https://pikepdf.readthedocs.io/en/latest/topics/security.html). Para la función especializada se propone [pyHanko](https://docs.pyhanko.eu/en/latest/).

**Conversión.** Exportar una página a PNG o extraer su texto no equivale a convertirla a DOCX conservando su estructura. PDF↔Office, conformidad PDF/A y accesibilidad PDF/UA necesitan investigación, herramientas y validación independientes. No forman parte del compromiso de la primera versión.

**Compresión.** Se distinguirá optimización sin pérdida de reducción de resolución/calidad de imágenes. No se garantiza un porcentaje fijo de reducción y no se aplicará degradación silenciosa.

## 6. Arquitectura propuesta

Separar cuatro responsabilidades:

1. Interfaz: ventanas, selección, herramientas y presentación del historial.
2. Aplicación: comandos de edición, validación, deshacer/rehacer y coordinación de tareas.
3. Adaptadores: motor PDF, OCR, firma, impresión y archivos.
4. Distribución: recursos, licencias, compilación y paquete MSIX.

Para la ruta C++, las modificaciones del documento se serializarán y el trabajo costoso se ejecutará fuera del hilo de interfaz, respetando las garantías del motor y Qt que se comprueben en el prototipo. Para la alternativa Python, un proceso de trabajo será propietario del documento y la interfaz recibirá resultados y progreso mediante comunicación entre procesos; las tareas de lectura paralelas usarán documentos independientes. PyMuPDF advierte que ejecutar su API en varios hilos puede provocar errores o cierres: [documentación de multiprocessing](https://pymupdf.readthedocs.io/en/latest/recipes-multiprocessing.html).

El guardado deberá escribir en un archivo temporal, comprobar que se puede reabrir y sustituir el destino únicamente al terminar correctamente. El historial puede requerir instantáneas para operaciones que no sean invertibles. Estos puntos son propuestas de diseño, todavía sin implementar.

## 7. Prueba de viabilidad antes de adoptar el stack

El siguiente entregable técnico recomendado es un prototipo pequeño. Debe comprobar:

- Abrir, renderizar y buscar en PDF de texto, escaneados, cifrados, con formularios y con páginas rotadas.
- Anotar, reordenar y guardar; reabrir con un visor independiente y comparar contenido y apariencia.
- Sustituir una frase con acentos y una imagen; documentar qué fuentes y diseños conserva.
- Verificar coordenadas de selección en distintos zoom, rotaciones y escalados de Windows.
- Mantener la interfaz operativa con un documento de 500 páginas; medir inicio, memoria, renderizado y cancelación en un equipo documentado.
- Compilar y ejecutar en un Windows limpio sin Python instalado, y generar un primer MSIX de ensayo.
- Probar distribución de bibliotecas, modelos y avisos de licencia; fijar versiones y hashes después del resultado.

No hay resultados todavía. Ensayar primero la integración de PDF4QT con una interfaz Qt propia, la edición localizada y el empaquetado MSIX. Mantener Python como alternativa si la complejidad del stack C++ supera el beneficio y se acepta su ruta de licencia. No comprometer fechas de una versión completa antes de esta validación.
