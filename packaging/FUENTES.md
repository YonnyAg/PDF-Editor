# Fuentes y bibliotecas de esta edición

El archivo `PDF-Editor-0.1.0-preview.1-source.zip` contiene el código propio MIT,
pruebas, iconos, scripts y parches. El ZIP de fuentes de dependencias contiene
Qt Base, Svg y Translations 6.9.3 originales, con sus sumas oficiales verificadas,
y el checkout de PDF4QT fijado en dependencies.json con los parches aplicados.
Se incluyen el inventario vcpkg y los parches necesarios para reconstruir.

Las dependencias nativas restantes se reconstruyen con vcpkg según el manifiesto
y los overlays de PDF4QT; sus licencias permiten esta distribución binaria y los
avisos completos se encuentran en licenses/vcpkg. El runtime de Microsoft es
redistribuible de Visual Studio y no está bajo MIT.

Consulta docs/DESARROLLO.md y docs/DESPLIEGUE.md dentro del ZIP del código.
La construcción requiere Windows x64, Visual Studio 2022 C++, CMake y Qt 6.9.3
MSVC2022 x64. scripts/setup-dependencies.ps1 fija las revisiones y aplica parches;
scripts/build.ps1 compila y prueba. scripts/package-release.ps1 crea la distribución
desde un runtime Release comprobado.

Qt se enlaza dinámicamente. Para probar una Qt modificada, conserva la misma ABI
y arquitectura, recompílala con MSVC compatible y reemplaza las DLL y plugins
correspondientes en una copia extraída de la distribución. No se prohíbe la
ingeniería inversa necesaria para depurar esas modificaciones. Los archivos de
fuentes entregados permiten obtener las licencias LGPL/GPL y avisos originales.

Origen Qt: https://download.qt.io/archive/qt/6.9/6.9.3/submodules/
Origen PDF4QT: https://github.com/JakubMelka/PDF4QT
Guía Qt: https://doc.qt.io/archives/qt-6.9/windows-deployment.html
Obligaciones Qt: https://www.qt.io/development/open-source-lgpl-obligations

El inventario de Qt incluye avisos de componentes de otras plataformas y módulos
de desarrollo por completitud; su presencia no significa que estén desplegados.
El manifiesto SHA256 enumera los archivos que sí forman parte de la aplicación.
