#ifndef StageDir
  #error StageDir is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
#ifndef PackageName
  #error PackageName is required
#endif

[Setup]
AppId={{D5CF3721-60F4-4894-8C73-6F0697F14414}
AppName=Juicy16
AppVersion={#AppVersion}
AppPublisher=Pokestir
AppPublisherURL=https://github.com/PokestirVGM/Juicy16
DefaultDirName={autopf}\Juicy16
DefaultGroupName=Juicy16
DisableProgramGroupPage=yes
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.14393
OutputDir={#OutputDir}
OutputBaseFilename={#PackageName}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Standalone\Juicy16.exe
LicenseFile={#StageDir}\LICENSE.txt
InfoBeforeFile={#StageDir}\INSTALL-WINDOWS.txt
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Types]
Name: "full"; Description: "VST3 plugin and standalone application"
Name: "plugin"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "plugin"; Description: "Juicy16 VST3 plugin"; Types: full plugin custom; Flags: fixed
Name: "standalone"; Description: "Standalone application for auditioning banks"; Types: full

[Files]
Source: "{#StageDir}\VST3\Juicy16.vst3\*"; DestDir: "{code:Vst3Directory}\Juicy16.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs; Components: plugin
Source: "{#StageDir}\Standalone\*"; DestDir: "{app}\Standalone"; Flags: ignoreversion recursesubdirs; Components: standalone
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "VST3\*,Standalone\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Juicy16"; Filename: "{app}\Standalone\Juicy16.exe"; Components: standalone
Name: "{group}\Installation guide"; Filename: "{app}\INSTALL-WINDOWS.txt"

[Code]
var PluginPage: TInputDirWizardPage;

function Vst3Directory(Param: String): String;
begin
  Result := PluginPage.Values[0];
end;

procedure InitializeWizard;
var DefaultPluginDir: String;
begin
  if IsAdminInstallMode then
    DefaultPluginDir := ExpandConstant('{commoncf64}\VST3')
  else
    DefaultPluginDir := ExpandConstant('{usercf}\VST3');
  PluginPage := CreateInputDirPage(wpSelectDir, 'VST3 plugin folder',
    'Choose where your DAW scans for VST3 plugins.',
    'The standard folder is recommended. Close your DAW before installing.', False, '');
  PluginPage.Add('VST3 folder:');
  DefaultPluginDir := GetPreviousData('Vst3Dir', DefaultPluginDir);
  PluginPage.Values[0] := ExpandConstant('{param:VST3DIR|' + DefaultPluginDir + '}');
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Vst3Dir', PluginPage.Values[0]);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = PluginPage.ID then begin
    if (ExtractFileDrive(PluginPage.Values[0]) = '') or
       ((Copy(PluginPage.Values[0], 2, 1) = ':') and
        (Copy(PluginPage.Values[0], 3, 1) <> '\')) then begin
      MsgBox('Choose an absolute VST3 folder path.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;
