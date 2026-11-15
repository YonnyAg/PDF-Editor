# Componentes de terceros

Este archivo registra los componentes incorporados en el prototipo. No sustituye
las licencias completas: antes de distribuir binarios deberán acompañarse los
textos y avisos de todas las dependencias transitivas, recursos y fuentes.

| Componente | Revisión usada | Licencia de referencia |
| --- | --- | --- |
| PDF4QT | v1.6.0.0 / `23f3829ae0420da801824a0d53d82c53baa2bee9` | MIT; texto en `third_party/licenses/PDF4QT.txt` |
| Qt | 6.9.3, módulos usados Core, Gui, Widgets, Concurrent, PrintSupport, Xml y Svg | LGPLv3 / GPLv3 / comercial según módulo; revisar también terceros incluidos |
| Blend2D y AsmJit | Revisiones fijadas por los overlays de PDF4QT | Zlib |
| OpenSSL | Fijada por el manifiesto vcpkg | Apache-2.0 |
| Little CMS | Fijada por el manifiesto vcpkg | MIT |
| zlib y libpng | Fijadas por el manifiesto vcpkg | zlib / libpng |
| FreeType | Fijada por el manifiesto vcpkg | FTL o GPLv2; revisar la elección para distribuir |
| Brotli | Dependencia transitiva de FreeType | MIT |
| bzip2 | Dependencia transitiva de FreeType | Licencia bzip2 |
| libjpeg-turbo | Fijada por el manifiesto vcpkg | BSD-3-Clause, IJG y Zlib según componentes |
| OpenJPEG | Fijada por el manifiesto vcpkg | BSD-2-Clause |

El núcleo de PDF4QT también incluye recursos como CMaps, certificados y fuentes.
Su auditoría y los avisos correspondientes forman parte de la preparación de una
distribución, no de una autorización implícita por usar el motor MIT.

vcpkg y aqtinstall son herramientas de desarrollo. `dependencies.json` fija sus
revisiones y las del motor. Las licencias completas instaladas por vcpkg quedan
en `.tools/vcpkg_installed/x64-windows/share/<paquete>/copyright`; Qt conserva
sus licencias en la instalación del SDK. El directorio `.tools` está excluido
de Git. El código propio y el icono se distribuyen bajo MIT (LICENSE).

La distribución 0.1.0-preview.1 incorpora textos completos en `licenses/`, con
avisos de Qt Base/Svg/Translations, componentes internos de Qt, paquetes vcpkg,
PDF4QT, CMaps de Adobe y fuentes Liberation (SIL OFL). Incluye el origen del
recurso AATL incorporado por PDF4QT y los inventarios del SDK. Se entregan fuentes
Qt y del motor en el ZIP de dependencias correspondiente; instrucciones en
FUENTES.md. El runtime de Microsoft Visual C++ se distribuye app-local como
componente redistribuible de Visual Studio, bajo sus condiciones de Microsoft.
Inno Setup es una herramienta de empaquetado y el stub de instalación conserva
sus condiciones; su licencia se incluye en el paquete. No se relicencian estas
dependencias bajo MIT ni se representa una certificación legal del conjunto.

Se aplica a PDF4QT el parche `third_party/patches/pdf4qt-core-only-translations.patch`:
evita configurar traducciones de aplicaciones que no existen en una compilación
del núcleo. El parche se conserva en el repositorio y se aplica mediante el script
de preparación; no cambia el lector ni el formato de los documentos.

El parche `third_party/patches/pdf4qt-name-tree-limits.patch` modifica el cargador
de árboles de nombres del núcleo: rechaza ciclos, nodos repetidos, pares incompletos,
claves duplicadas y recorridos que exceden los límites establecidos. Conserva
la licencia y los avisos originales. Se aplica mediante el mismo script de
preparación y requiere recompilar la biblioteca del motor.

Referencias: [PDF4QT v1.6.0.0](https://github.com/JakubMelka/PDF4QT/tree/v1.6.0.0),
[licencias Qt](https://doc.qt.io/qt-6/licensing.html).
