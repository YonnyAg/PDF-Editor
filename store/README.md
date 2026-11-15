# PDF Editor Open — Microsoft Store

Producto reservado el 15 de noviembre de 2026 en la cuenta que publica PixelArt
Studio. Nombre de la ficha: **PDF Editor Open**; editor: **YonnyAg**; gratuito.
Store ID: `9P8Z4GWSH6H8`. La identidad de `identity.json` fue leída de Partner Center,
no es una contraseña ni un certificado. No reutilizar la identidad de PixelArt.

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
La reserva o la carga del paquete no equivale a aprobación ni publicación.

Referencias: [requisitos MSIX](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements),
[manifiesto Win32](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-manual-conversion),
[licencia MIT](https://opensource.org/license/mit).
