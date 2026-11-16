# Preparación para GitHub y Microsoft Store

Actualización del 16 de noviembre de 2026: el código propio es MIT y está publicado
en [YonnyAg/PDF-Editor](https://github.com/YonnyAg/PDF-Editor), junto con las descargas
externas de la versión preliminar. **PDF Editor Open está aprobado y disponible
gratuitamente en [Microsoft Store](https://apps.microsoft.com/detail/9P8Z4GWSH6H8)**,
con el editor YonnyAg. El instalador y ZIP externos conservan su estado sin firma
Authenticode. Las guías vigentes son [DESPLIEGUE.md](DESPLIEGUE.md) y
[store/README.md](../store/README.md). La investigación y listas siguientes son
antecedentes del plan inicial, no el estado actual de publicación.

Fecha de investigación: 4 de noviembre de 2026. Este documento es una hoja de trabajo; no acredita que el programa esté listo ni que haya sido aceptado por Microsoft.

## GitHub

Antes de publicar la primera versión:

- Elegir nombre y comprobar su disponibilidad y posibles conflictos de marca.
- Crear el repositorio público con una descripción que refleje el estado real del producto.
- Incorporar LICENSE tras decidir motor y licencia; incluir avisos y licencias de terceros.
- Añadir CONTRIBUTING, código de conducta, política de seguridad y plantillas de incidencias.
- Documentar instalación, ejecución, compilación y pruebas reproducibles.
- Configurar integración continua para formato, análisis y pruebas de operaciones de documento.
- Usar exclusivamente documentos de prueba sintéticos o con permiso de redistribución.
- Publicar versiones etiquetadas, notas de cambios, ejecutables y sumas de comprobación.
- Mantener correspondencia entre cada binario, su código fuente, dependencias e instrucciones de construcción.
- Añadir capturas reales y actualizar el README con las funciones efectivamente disponibles.

La primera versión pública debería resolver bien lectura, organización de páginas y guardado. Edición avanzada, OCR y firmas se incorporarán en versiones posteriores cuando estén verificadas.

## Microsoft Store

### Ruta propuesta

1. Validar una aplicación autónoma de Windows x64. ARM64 requerirá verificar soporte y binarios de todas las dependencias.
2. Probar la compilación C++ con CMake y MSVC, gestionar dependencias con vcpkg y reunir las bibliotecas Qt dinámicas y recursos necesarios. Si se retoma la alternativa Python, evaluar [pyside6-deploy](https://doc.qt.io/qtforpython-6/deployment/deployment-pyside6-deploy.html). Ninguna de estas rutas sustituye la creación del MSIX.
3. Crear el paquete MSIX con identidad de aplicación, editor, versión, iconos, requisitos de sistema y asociación con `.pdf`.
4. Registrar o utilizar una cuenta de desarrollador en Partner Center y reservar el nombre del producto.
5. Hacer coincidir los datos del manifiesto con la identidad asignada por Partner Center.
6. Probar instalación, actualización, desinstalación, apertura de archivos, impresión y ejecución sin privilegios administrativos en un equipo limpio.
7. Completar descripción, capturas, categoría, clasificación por edades, contacto de soporte, información de privacidad y notas para certificación.
8. Enviar el paquete y resolver las observaciones de certificación.

MSIX es la ruta propuesta, no una obligación universal para toda aplicación de escritorio de la Store. Microsoft también documenta otras vías de distribución; la selección definitiva dependerá del prototipo y sus dependencias. Fuentes: [publicar una aplicación Windows](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/publish-first-app) y [opciones de publicación](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/overview).

### Firma y cuenta

Para paquetes MSIX/AppX enviados a Store, Microsoft documenta que vuelve a firmarlos después de la certificación; no se requiere una firma de una autoridad comercial para ese envío. La instalación de prueba fuera de Store y la distribución directa por GitHub necesitan su propia estrategia de confianza y firma. Véanse [requisitos de paquetes](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/app-package-requirements?pivots=store-installer-msix).

La documentación consultada indica que la nueva experiencia de registro no cobra inscripción para cuentas Individual o Company. La elección depende de quién publica y la verificación sigue siendo necesaria. Confirmar el flujo aplicable a la cuenta y región al registrarse; no presupuestar usando tarifas históricas. Fuente: [apertura de una cuenta de desarrollador](https://learn.microsoft.com/en-us/windows/apps/publish/partner-center/open-a-developer-account).

### Privacidad y calidad del producto

La intención de diseño es procesar documentos localmente, sin cuenta obligatoria. La declaración de privacidad final describirá el comportamiento realmente implementado: diagnósticos, actualizaciones, archivos recientes y posibles conexiones para validar certificados o sellos de tiempo. Firmas con servicios externos pueden requerir red aunque la edición se realice localmente.

Preparar una política pública de privacidad, una página de soporte y una ficha fiel a las capacidades disponibles. El paquete deberá superar pruebas de funcionamiento, accesibilidad, tratamiento de archivos y actualización. Revisar licencias sobre el paquete final, incluyendo motores nativos, Qt, OCR, fuentes y modelos de idioma.

Las [políticas de Microsoft Store](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies) se consultaron en su versión 7.20, efectiva desde el 22 de octubre de 2026. Deben revisarse antes del envío: la certificación no se puede garantizar durante esta etapa.

## Decisiones pendientes

| Decisión | Propuesta actual | Evidencia necesaria |
| --- | --- | --- |
| Nombre y marca | PDF Editor como nombre de trabajo | Disponibilidad y preferencia del autor |
| Lenguaje y motor | C++20 + Qt 6 Widgets + núcleo de PDF4QT; lectura y visor inicial integrados | Ampliar pruebas con documentos complejos y validar empaquetado |
| Licencia | Evaluar MIT para nuestro código; Qt y terceros conservan sus condiciones | Revisión de versiones y condiciones de distribución |
| Plataformas | Windows 11 x64 inicialmente | Validación en equipos limpios y dependencias |
| Primer lanzamiento | Lectura y organización con guardado fiable | Pruebas funcionales y visuales |
| Cuenta Store | Elegir según titular y uso del proyecto | Flujo de Partner Center del autor |

El remoto de este directorio es https://github.com/YonnyAg/PDF-Editor.git y la
primera edición MSIX ya está publicada en Microsoft Store. Los datos de identidad
y la preparación de futuras actualizaciones se documentan en ../store/README.md.
