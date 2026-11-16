# PDF Editor Open — Microsoft Store

Producto reservado el 15 de noviembre de 2026 en la cuenta que publica PixelArt
Studio. Nombre de la ficha: **PDF Editor Open**; editor: **YonnyAg**; gratuito.
Store ID: `9P8Z4GWSH6H8`. La identidad de `identity.json` fue leída de Partner Center,
no es una contraseña ni un certificado. No reutilizar la identidad de PixelArt.

**Estado confirmado el 16 de noviembre de 2026: aprobado y disponible en Microsoft Store.**

**[Instalar PDF Editor Open desde Microsoft Store](https://apps.microsoft.com/detail/9P8Z4GWSH6H8)**

El propietario confirmó el estado «En Microsoft Store» en Partner Center y se
comprobó la ficha pública, con el editor YonnyAg y el botón de descarga gratuita.
El envío inicial se realizó el 15 de noviembre. Precio, propiedades, clasificación,
paquete, ficha y opciones quedaron completos.
Se cargó y validó PDF-Editor-Open-1.0.0.0-x64.msix, con SHA256
`A92EA748172ABF6BA20684A2FB2EFB83D37F31D3A05894ED09E432F12BF955EC`.
La ficha en español incluye cuatro capturas reales y los enlaces de privacidad
y soporte. IARC calculó 3+/Everyone o todas las edades según región; el propietario
autorizó el envío de sus datos y confirmó la aceptación y mayoría de edad.
El envío superó la certificación y la publicación automática quedó completada.
La aplicación sigue siendo una edición preliminar con los límites funcionales
documentados. La aprobación no acredita compatibilidad universal de PDFs ni una
auditoría independiente del código.

La integridad de los 355 archivos del runtime se comprobó dentro del MSIX y pasó
la validación MakeAppx. Las compilaciones y pruebas de GitHub Actions pasaron.
Antes del envío no se completó una instalación local de este MSIX ni Windows App Certification
Kit: el equipo no tenía habilitado el modo de desarrollador y no se cambiaron
sus ajustes de seguridad. La validación de carga no sustituye esas pruebas ni la
certificación. Informes locales en build/store/qa y build/store/1.0.0.0.

La compilación de GitHub conserva el nombre PDF Editor y 0.1.0-preview.1. El
primer MSIX utiliza el contador de paquete `1.0.0.0`, con revisión final 0 para
Store; ese número no convierte la edición preliminar en una edición estable.

## Empaquetado

El runtime de entrada es la carpeta de distribución ya verificada (sin pruebas,
PDB ni instalador Inno). Para generar los iconos de Store, compilar el generador
Qt de tools/icon-generator y ejecutarlo con el SVG, store/assets y --store.
Los PNG generados quedan en el repositorio para reconstruir sin el generador.

```powershell
.\scripts\package-store.ps1 -RuntimeDirectory '.\build\releases\0.1.0-preview.1\PDF Editor'
```

El script verifica el manifiesto SHA256 del runtime y la identidad, crea el
manifiesto MSIX, incorpora iconos y valida con MakeAppx del Windows SDK.
El destino predeterminado es `build/store/1.0.0.0`. Rechaza staging existente;
usar otro OutputDirectory para conservar compilaciones anteriores.
El paquete declara Windows 11 x64, es-ES, runFullTrust y apertura de `.pdf` por
argumento de línea de comandos. No cambia automáticamente el lector predeterminado.

El MSIX sin firma está destinado al envío a Microsoft Store. No es un instalador
público para abrir directamente. Microsoft firma el paquete después de su
certificación. El EXE de GitHub conserva su estado de firma independiente.

## Licencias y ficha

MIT permite publicar el código propio en la Store conservando sus avisos. Qt y
el resto de los componentes mantienen sus condiciones: el paquete incluye las
licencias y los inventarios de la distribución de GitHub. Se conservan las fuentes
y el runtime portátil para modificar y sustituir bibliotecas compatibles de Qt.
No se prohíbe la ingeniería inversa necesaria para depurar esas modificaciones.

Soporte: https://github.com/YonnyAg/PDF-Editor/issues
Privacidad: https://github.com/YonnyAg/PDF-Editor/blob/main/PRIVACY.md
La ficha debe describir la edición preliminar y sus límites, sin prometer edición
universal, PDF/UA ni validación avanzada de firmas. Las capturas deben ser reales,
con documentos sintéticos. No reutilizar capturas ni la clasificación de PixelArt.

Validar instalación y activación del MSIX en Windows, además del esquema MakeAppx,
y revisar el informe de Windows App Certification Kit antes del envío final.
Los cuestionarios de edad y acuerdos deben reflejar el comportamiento de este
producto; el propietario revisa las declaraciones legales que exija Partner Center.
Para futuras actualizaciones, la reserva o la carga del paquete por sí solas no
equivalen a aprobación ni publicación.

Referencias: [requisitos MSIX](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements),
[manifiesto Win32](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-manual-conversion),
[licencia MIT](https://opensource.org/license/mit).
