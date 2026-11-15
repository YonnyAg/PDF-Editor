#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef ReleaseVersion
  #error ReleaseVersion is required
#endif
#ifndef WindowsVersion
  #error WindowsVersion is required
#endif
#ifndef ProductAppId
  #error ProductAppId is required
#endif
[Setup]
AppId={#ProductAppId}
AppName=PDF Editor
AppVersion={#ReleaseVersion}
AppVerName=PDF Editor {#ReleaseVersion}
AppPublisher=PDF Editor
VersionInfoVersion={#WindowsVersion}
VersionInfoDescription=Instalador de PDF Editor
DefaultDirName={localappdata}\Programs\PDF Editor
DefaultGroupName=PDF Editor
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputDir}
OutputBaseFilename=PDF-Editor-{#ReleaseVersion}-windows-x64-setup
SetupIconFile=..\..\resources\branding\pdf-editor.ico
UninstallDisplayIcon={app}\pdf_editor.exe
LicenseFile=..\..\LICENSE
InfoBeforeFile=..\LEEME.txt
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes
UninstallDisplayName=PDF Editor
[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; Flags: unchecked
Name: "openwith"; Description: "Añadir PDF Editor a las opciones de Abrir con para archivos PDF"; Flags: unchecked
[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\PDF Editor"; Filename: "{app}\pdf_editor.exe"
Name: "{group}\Desinstalar PDF Editor"; Filename: "{uninstallexe}"
Name: "{autodesktop}\PDF Editor"; Filename: "{app}\pdf_editor.exe"; Tasks: desktopicon
[Registry]
Root: HKCU; Subkey: "Software\Classes\PDFEditor.Document"; ValueType: string; ValueData: "Documento PDF"; Flags: uninsdeletekey; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\PDFEditor.Document\DefaultIcon"; ValueType: string; ValueData: """{app}\pdf_editor.exe"",0"; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\PDFEditor.Document\shell\open\command"; ValueType: string; ValueData: """{app}\pdf_editor.exe"" ""%1"""; Tasks: openwith
Root: HKCU; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueName: "PDFEditor.Document"; ValueType: string; ValueData: ""; Flags: uninsdeletevalue; Tasks: openwith
