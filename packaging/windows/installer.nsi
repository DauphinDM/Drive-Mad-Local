Unicode true
!include "MUI2.nsh"

!ifndef VERSION
!define VERSION "1.0.0"
!endif
!ifndef STAGE
!define STAGE "stage"
!endif
!ifndef OUTFILE
!define OUTFILE "DriveMad-setup.exe"
!endif
!ifndef ICONFILE
!define ICONFILE "..\..\packaging\icon.ico"
!endif

Name "Drive Mad ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\DriveMad"
InstallDirRegKey HKCU "Software\DriveMad" "InstallDir"
RequestExecutionLevel user
Icon "${ICONFILE}"
UninstallIcon "${ICONFILE}"
ManifestDPIAware true
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Drive Mad"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "CompanyName" "Drive Mad Local"
VIAddVersionKey "FileDescription" "Drive Mad native game installer"
VIAddVersionKey "LegalCopyright" "Fancade game assets; native host by Drive Mad Local"

!define MUI_ABORTWARNING
!define MUI_ICON "${ICONFILE}"
!define MUI_UNICON "${ICONFILE}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Drive Mad" SecMain
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  CreateDirectory "$SMPROGRAMS\Drive Mad"
  CreateShortCut "$SMPROGRAMS\Drive Mad\Drive Mad.lnk" "$INSTDIR\drive-mad.exe"
  CreateShortCut "$DESKTOP\Drive Mad.lnk" "$INSTDIR\drive-mad.exe"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\DriveMad" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad" \
    "DisplayName" "Drive Mad"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad" \
    "Publisher" "Drive Mad Local"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad" \
    "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad" \
    "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad" \
    "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  Delete "$DESKTOP\Drive Mad.lnk"
  Delete "$SMPROGRAMS\Drive Mad\Drive Mad.lnk"
  RMDir "$SMPROGRAMS\Drive Mad"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DriveMad"
  DeleteRegKey HKCU "Software\DriveMad"
SectionEnd
