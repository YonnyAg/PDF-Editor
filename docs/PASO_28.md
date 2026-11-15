# Paso 28 — distribución Windows

Se prepara **PDF Editor 0.1.0-preview.1**, conservando el alcance funcional y el
motor corregido del paso 27. El código propio y el icono original se licencian
bajo MIT por decisión del autor. El nombre del editor en los metadatos es el del
proyecto, sin asignar una identidad personal o jurídica no proporcionada.

El ejecutable incorpora icono de diez tamaños, versión y metadatos de Windows.
Se añaden --version y --profile para identificar la edición y aislar sesiones.
El paquete incorpora los diálogos estándar de Qt en español.

Se generan un instalador EXE por usuario y un ZIP portátil, con Qt, plugins,
motor y runtime MSVC app-local. Se excluyen pruebas, símbolos y herramientas.
El registro opcional de «Abrir con» conserva la aplicación PDF predeterminada.
La desinstalación preserva documentos y perfiles locales.

La distribución incluye MIT, avisos y licencias de bibliotecas y recursos,
inventarios, instrucciones de sustitución de Qt, fuentes de dependencias y del
código propio. Cada carpeta de aplicación tiene un manifiesto SHA256 y los
artefactos tienen SHA256SUMS.txt. No se incluyen documentos personales ni claves.

Las fuentes permanecen en D. La compilación se realiza en objetos temporales de
C y se copian los binarios a D con verificación SHA256. Pasaron 34 suites CTest
(17 Debug y 17 Release), 35 flujos nativos de interfaz y los núcleos adicionales
con el backend Windows. Se verificaron 124 archivos; se actualizaron los
ejecutables habituales. Informes: build/paso28/qa/comprobacion-en-D.

La prueba del instalador y del ZIP abre un PDF sintético con espacios y acentos
en rutas con espacios, PATH limitado a Windows y perfiles independientes.
Comprueba que los módulos se carguen desde el producto o Windows, verifica los
hashes del ZIP y de cada instalación y exige cierre limpio con sesión guardada.
Prueba reinstalación, registro opcional de «Abrir con», conservación de UserChoice,
documento y perfil, y desinstalación de archivos, accesos y registros propios.
El resultado definitivo está en build/paso28/qa/distribution-complete/resultado.json.

No se ha realizado una publicación externa. Los binarios son preliminares y
carecen de firma Authenticode. Faltan la firma con identidad real, QA en otro
equipo limpio, contacto de soporte y el destino público de publicación. La
identidad y certificación de Microsoft Store son un trabajo adicional. Consulta
DESPLIEGUE.md para reproducir el empaquetado y continuar la publicación.
