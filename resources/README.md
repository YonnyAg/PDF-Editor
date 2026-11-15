# Recursos de la aplicación

`branding/pdf-editor.svg` es el icono original del proyecto, bajo MIT. Sus PNG
y el ICO de diez tamaños se regeneran con `scripts/generate-icons.ps1` y Qt Svg.
El ejecutable incorpora los PNG como recursos Qt y el ICO como recurso Windows.
La distribución genera la traducción española de Qt desde la fuente fijada en
`packaging/qt-sources.json`; conserva sus avisos y licencia originales.
