#define AppName "Galaxy ATM System"
#define Payload "..\dist\Galaxy ATM System"
#ifndef AppVersion
  #define AppVersion GetDateTimeString('yyyy.mm.dd', '.', '')
#endif

[Setup]
AppId={{414CC654-49A5-4A91-A69F-5FB66F84A3CB}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Yurko-v
DefaultDirName={userdocs}\{#AppName}
DirExistsWarning=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
CloseApplications=yes
CloseApplicationsFilter=*.dll
RestartApplications=no
OutputDir=..\dist
OutputBaseFilename=GalaxyATMSystemSetup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
UninstallFilesDir={app}\uninstall

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ru.FontsTask=Установить шрифт Inter (список РЦ)
en.FontsTask=Install the Inter font (sector list)
ru.OpenFolder=Открыть папку плагина
en.OpenFolder=Open the plugin folder
ru.LoadInEuroScope=Плагин установлен в:%n%1%n%nВ EuroScope откройте OTHER SET > Plug-ins, нажмите Load и выберите GalaxyATMSystem.dll. Если плагин уже был загружен из этой папки, просто перезапустите EuroScope.
en.LoadInEuroScope=The plugin is installed in:%n%1%n%nIn EuroScope open OTHER SET > Plug-ins, press Load and pick GalaxyATMSystem.dll. If the plugin was already loaded from this folder, just restart EuroScope.

[Tasks]
Name: "fonts"; Description: "{cm:FontsTask}"

[Files]
Source: "..\Release\GalaxyATMSystem.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\GalaxyATMSystem.json"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Payload}\Zones.ULLL.json"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Inter-Regular.otf"; DestDir: "{autofonts}"; FontInstall: "Inter Regular (TrueType)"; Tasks: fonts; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Inter-Medium.otf"; DestDir: "{autofonts}"; FontInstall: "Inter Medium (TrueType)"; Tasks: fonts; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\Inter-SemiBold.otf"; DestDir: "{autofonts}"; FontInstall: "Inter Semi Bold (TrueType)"; Tasks: fonts; Flags: onlyifdoesntexist uninsneveruninstall
Source: "fonts\LICENSE.txt"; DestDir: "{app}"; DestName: "Inter-LICENSE.txt"; Tasks: fonts; Flags: ignoreversion

[Run]
Filename: "{app}"; Description: "{cm:OpenFolder}"; Flags: postinstall shellexec skipifsilent nowait unchecked

[Code]
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    WizardForm.FinishedLabel.Caption := FmtMessage(CustomMessage('LoadInEuroScope'), [ExpandConstant('{app}')]);
    WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
    WizardForm.RunList.Top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(12);
  end;
end;
