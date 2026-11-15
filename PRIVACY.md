# Privacidad de PDF Editor / PDF Editor Open

Última actualización: 15 de noviembre de 2026. Editor: YonnyAg.

PDF Editor procesa los documentos localmente. La aplicación no incorpora cuentas,
publicidad, telemetría ni actualización automática propia. Abrir, editar, buscar,
extraer texto, realizar OCR y guardar no envía los PDFs a nuestros servidores.
Los idiomas OCR dependen de los paquetes instalados en Windows.

## Datos guardados en el equipo

Se conservan las rutas de archivos recientes, la sesión y copias de recuperación
de los cambios pendientes. Estas copias pueden contener contenido del documento.
La versión de GitHub usa los datos locales del perfil de Windows bajo
`PDFEditor/PDF Editor/sessions`. Windows puede redirigir esa ubicación al
almacenamiento privado del paquete cuando se instala desde Microsoft Store.
`--profile "directorio"` permite elegir un perfil independiente.

Los documentos guardados o exportados se escriben en la ubicación que elijas.
Protege tu cuenta y el almacenamiento del equipo. La desinstalación MSIX puede
eliminar los datos privados del paquete: guarda las copias que quieras conservar
en una carpeta de documentos antes de desinstalar.

## Servicios del sistema y enlaces

La impresión utiliza la cola y el controlador elegidos por el usuario. Las firmas
PDF utilizan el certificado seleccionado y la comprobación local implementada;
esta edición no ofrece sellado de tiempo remoto. Los enlaces externos se abren
en el navegador cuando el usuario decide seguirlos. Windows, Microsoft Store,
los controladores y otras aplicaciones tienen sus propias políticas y servicios.

## Soporte

Utiliza [las incidencias del proyecto](https://github.com/YonnyAg/PDF-Editor/issues)
para consultas generales. No adjuntes documentos confidenciales, contraseñas,
certificados PFX/P12, claves o registros con información personal. Consulta
[SECURITY.md](SECURITY.md) para comunicar un problema de seguridad.
