# Seguridad

La edición 0.1.0-preview.1 es preliminar. No se garantiza una fecha de respuesta
ni un período de soporte. Mantén copias de los originales y revisa los documentos
guardados antes de compartirlos. La aplicación procesa archivos potencialmente
hostiles y utiliza bibliotecas nativas; ninguna batería de pruebas garantiza
compatibilidad o seguridad con todos los PDFs.

No publiques contraseñas, documentos confidenciales, certificados PFX/P12,
claves privadas ni una prueba de explotación con datos personales en incidencias.
Si GitHub ofrece «Report a vulnerability» en la pestaña Security del repositorio,
utiliza ese canal privado. Si no está disponible, abre una incidencia indicando
únicamente que necesitas un canal privado, sin adjuntar detalles de explotación.

La firma criptográfica de un PDF y la firma Authenticode del programa son cosas
distintas. Los binarios preliminares actuales no llevan firma Authenticode.
Descarga los artefactos de Releases del repositorio YonnyAg/PDF-Editor y compara
sus hashes con SHA256SUMS.txt. Esa comprobación verifica integridad, no sustituye
la firma del editor ni una auditoría independiente.
