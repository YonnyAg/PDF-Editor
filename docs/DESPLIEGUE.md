# Distribución Windows — 0.1.0-preview.1

Nombre: **PDF Editor**. Código propio, icono y scripts: **MIT** por decisión del
autor el 15 de noviembre de 2026. «PDF Editor contributors» identifica al proyecto,
sin atribuir un nombre personal o una empresa. El identificador del instalador
está fijado en packaging/release.json y debe mantenerse entre actualizaciones.

## Artefactos

En `build/releases/0.1.0-preview.1/`:

- `PDF-Editor-0.1.0-preview.1-windows-x64-setup.exe`: instalador para el usuario actual.
- `PDF-Editor-0.1.0-preview.1-windows-x64-portable.zip`: extraer completo y abrir pdf_editor.exe.
- `PDF-Editor-0.1.0-preview.1-source.zip`: código propio con licencia, icono y pruebas.
- `PDF-Editor-0.1.0-preview.1-dependency-sources.zip`: Qt original y PDF4QT con parches.
- `SHA256SUMS.txt`: sumas de los cuatro artefactos.

El instalador requiere Windows 11 x64, no necesita administrador y usa
`%LOCALAPPDATA%/Programs/PDF Editor`. Crea accesos en el menú Inicio. El acceso
de escritorio y el registro en «Abrir con» son opcionales; no se altera UserChoice.
Reinstalar sobre la misma ruta actualiza los archivos. La desinstalación conserva
los documentos y la sesión local del usuario.

La versión portátil usa también la carpeta local de sesión de Windows. Para
aislar una sesión, ejecutar `pdf_editor.exe --profile "directorio" archivo.pdf`.
`--version` identifica esta edición. Los diálogos Qt se traducen al español
mediante qtbase_es.qm. El icono original se conserva en SVG y en diez tamaños PNG/ICO.

## Reconstrucción

Preparar las dependencias y compilar Release siguiendo DESARROLLO.md. El runtime
de entrada debe contener el ejecutable, el motor corregido y sus DLL nativas.
No utilizar la biblioteca anterior a la corrección del paso 23. Para empaquetar:

```powershell
.\scripts\generate-icons.ps1
.\scripts\package-release.ps1 -RuntimeDirectory .\build\paso28\Release `
    -InnoCompiler .\.tools\InnoSetup-7.1.1\ISCC.exe `
    -Python python
```

Instalar Inno Setup 7.1.1 desde su sitio oficial y verificar la firma de Pyrsys B.V.
Descargar los tres archivos Qt de packaging/qt-sources.json a `.tools/release-downloads`.
El script comprueba sus SHA256 antes de extraer avisos y compilar la traducción.
El compilador de instaladores y su instalador no se redistribuyen con el producto.
Si ya existe la carpeta de staging, usar otro `-OutputDirectory`: el script no
elimina distribuciones anteriores. El SDK Qt, Python y Visual Studio son requisitos
del empaquetador, no del equipo que ejecuta el programa.

`windeployqt` reúne Qt y sus plugins; se añaden explícitamente el motor, las
bibliotecas vcpkg y el runtime MSVC app-local. La carpeta del producto excluye
ejecutables de prueba, PDB, herramientas de compilación y Qt Test. Se entrega
`manifest-sha256.json` con los hashes de su contenido, además de las licencias,
inventarios y fuentes correspondientes. Las DLL de Qt se pueden reemplazar por
versiones compatibles. Véase packaging/FUENTES.md y THIRD_PARTY_NOTICES.md.

El icono se regenera con Qt Svg/Gui, sin herramientas gráficas externas. La versión
visible y los cuatro números de Windows proceden de packaging/release.json;
el archivo .rc incorpora el icono y los metadatos al ejecutable.

## Validación y publicación

Las pruebas funcionales del paso 28 se registran en `build/paso28/qa`. La prueba
de distribución está en `scripts/test-release.ps1`: usa carpetas y perfiles de QA,
comprueba los hashes, abre un PDF sintético desde la edición portátil y la instalada,
prueba reinstalación y desinstalación y verifica conservación del documento y perfil.
Rechaza iniciar si detecta una instalación previa con el mismo identificador.
Ejecutar con PowerShell 7 desde Windows; no modifica una instalación personal.

Los binarios externos de GitHub **no llevan firma Authenticode**. La firma de PDF
es una función diferente. Para ofrecer descargas externas firmadas falta firmar
con una identidad real del editor, repetir QA y regenerar sus sumas.
No se debe sustituir esa firma por un certificado de pruebas presentado como fiable.

También falta validar en otro equipo limpio de Windows 11 x64, con rutas y escalas
distintas, impresión, idiomas OCR y tecnologías de apoyo. La prueba local reduce
dependencias accidentales pero no equivale a una VM limpia ni a certificación.

El repositorio público es https://github.com/YonnyAg/PDF-Editor. La versión de
GitHub incluye cuatro artefactos y SHA256SUMS.txt. PDF Editor Open está aprobado
y disponible en https://apps.microsoft.com/detail/9P8Z4GWSH6H8, confirmado el
16 de noviembre de 2026. El paquete publicado en Store es el MSIX de identidad
YonnyAg.PDFEditorOpen; el instalador EXE externo es un artefacto diferente.
La construcción y el historial de la edición Store están en ../store/README.md.

Referencias: [despliegue Qt 6.9](https://doc.qt.io/archives/qt-6.9/windows-deployment.html),
[fuentes Qt 6.9.3](https://download.qt.io/archive/qt/6.9/6.9.3/submodules/),
[Inno Setup](https://jrsoftware.org/inno-setup/downloads),
[Qt LGPL](https://www.qt.io/development/open-source-lgpl-obligations),
[runtime redistribuible de Microsoft](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files),
[registro en Abrir con](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box).

