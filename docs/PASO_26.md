# Paso 26: contenido existente, objetos insertados y firmas

Este bloque incorpora una primera implementación de los tres puntos solicitados.
Tiene límites de compatibilidad explícitos; no termina toda la edición avanzada
ni equivale a completar el proyecto.

## Texto e imágenes existentes

En **Archivo → Editar contenido de esta página…**, selecciona un bloque de texto,
una imagen o un trazado de la lista. La vista previa delimita el objeto actual.
Puedes sustituir texto simple de una línea usando su fuente original o reemplazar
una imagen. El texto nuevo debe usar caracteres disponibles en la fuente: una
fuente incrustada como subconjunto puede no contener letras que no aparecían antes.
No se sustituyen párrafos complejos ni se añaden fuentes de reemplazo automáticamente.

La imagen nueva se ajusta a la caja existente, pudiendo cambiar su proporción;
la transparencia se compone sobre blanco. Se admiten imágenes de hasta 16 millones
de píxeles y 8192 píxeles por lado.

Antes de aceptar una edición se reconstruye la página sin cambios y se compara
su representación con la original, hasta 1200 píxeles por lado. Si difiere o hay
advertencias, la operación se rechaza. Esta comprobación de la vista previa no
es una garantía universal de fidelidad a cualquier resolución. Se rechazan
documentos con capas opcionales o estructura etiquetada y recursos ambiguos.
Los documentos cifrados, firmados o XFA conservan sus restricciones de edición.

## Objetos y estilos después de guardar

El mismo diálogo permite mover objetos por porcentajes de la página, escalarlos
entre el 10 y el 400 %, girarlos desde su centro o eliminarlos. El resultado
transformado debe quedar dentro del área visible. Los trazados permiten nuevos
colores, relleno y grosor de 0,5 a 20 puntos. Se aplican cambios individuales,
con historial y guardado; los objetos compatibles se pueden volver a editar
tras cerrar y reabrir el PDF, incluidos textos e imágenes insertados.

En **Anotar → Formas de esta página… → Cambiar estilo…** puedes aplicar nuevos
colores y grosor a formas y dibujos a mano alzada ya guardados. Rectángulos y
elipses admiten relleno. La apariencia se regenera y el cambio puede deshacerse.
Esta operación aplica los valores elegidos y un borde sólido. La edición de
puntos individuales de un trazo y el movimiento de anotaciones siguen pendientes.

Eliminar objetos desde el editor no es una herramienta de supresión de datos
sensibles: pueden quedar recursos sin uso. Para ese fin se conserva la herramienta
del paso 25, que reconstruye toda la copia como imágenes.

## Firma y verificación criptográfica

**Archivo → Firmar copia con certificado…** utiliza un archivo PFX/P12 del usuario
que contenga su clave privada. Solicita la contraseña y un motivo opcional y crea
un destino nuevo, incluyendo los cambios pendientes y conservando el documento
activo. La firma es invisible, SHA-256 y PKCS#7 separada, con un campo AcroForm.
No modifica el original ni importa certificados o contraseñas al almacén de la
aplicación. Se verifican integridad y cobertura antes de publicar.

Abre la copia generada y usa **Archivo → Verificar firmas digitales…**. El informe
separa integridad criptográfica, confianza/vigencia del certificado y cobertura
del archivo completo. Un certificado autofirmado puede tener integridad correcta
y confianza no verificada. La verificación usa las raíces disponibles en el sistema
y los recursos locales del motor; no consulta revocación en línea.

La verificación trabaja con el archivo guardado. Si cambió en el disco, exige
reabrirlo; si hay cambios pendientes, la acción queda desactivada. Se admiten PDFs
de hasta 100 MB y hasta 100 firmas, con dos rangos de bytes por firma y límites
en el árbol de campos. El PFX/P12 se limita a 4 MB.

No se añaden firmas a archivos ya firmados, ni hay sellado de tiempo, OCSP,
validación de larga duración, firma visible con certificado o integración con
tarjetas y dispositivos criptográficos. Se verificó un certificado RSA de 2048
bits creado exclusivamente para pruebas; otros certificados requieren validación.

## Validación y siguientes bloques

Las pruebas cubren cuatro giros, recorte, origen desplazado y UserUnit; sustitución,
transformación, estilo, eliminación, historial y reapertura; firma real, contraseña
incorrecta, alteración de contenido y rangos/campos malformados. Además de PDF4QT,
Windows SignedCms comprueba de forma independiente la firma separada SHA-256.

Siguen pendientes edición avanzada de párrafos y fuentes, compatibilidad ampliada,
edición de puntos de trazos, firmas avanzadas, formularios avanzados, accesibilidad,
instalador, licencia definitiva y publicación.

Ejecutable: `build/paso26/Release/pdf_editor.exe`. Informes: `build/paso26/qa`.

Validación final: 30 conjuntos CTest y 31 flujos nativos de interfaz desde D, sin
fallos, además del núcleo nuevo con el backend Windows. Se verificaron 120 archivos
por SHA256 y se revisaron las capturas de edición y verificación. Los ejecutables
habituales de Debug y Release también se actualizaron.
