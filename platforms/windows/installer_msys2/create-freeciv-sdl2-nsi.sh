#!/bin/sh

# ./create-freeciv-sdl2-nsi.sh <freeciv files dir> <output dir> <version> <win32|win64|win>

ARCH_KEY_PART="$4"
if test "$4" != "win32" && test "$4" != "win64" ; then
  ARCH_INST_PART="-${ARCH_KEY_PART}"
else
  ARCH_INST_PART=""
fi

cat <<EOF
; Freeciv Windows installer script
; some parts adapted from Wesnoth installer script

Unicode true
SetCompressor /SOLID lzma

!define APPNAME "Freeciv"
!define VERSION $3
!define GUI_ID sdl2
!define GUI_NAME SDL2
!define WIN_ARCH $4
!define ARCH_KEY_PART ${ARCH_KEY_PART}
!define ARCH_INST_PART ${ARCH_INST_PART}
!define KEYROOT "Freeciv"
!define APP_KEY_PART "client-\${GUI_ID}"

!define APPID "\${APPNAME}-\${VERSION}\${ARCH_INST_PART}-\${GUI_ID}"

!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART}"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME ""
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME ""
!define MULTIUSER_INSTALLMODE_INSTDIR "\${APPNAME}-\${VERSION}\${ARCH_INST_PART}-\${GUI_ID}"

!include "MultiUser.nsh"
!include "MUI2.nsh"
!include "nsDialogs.nsh"

;General

Name "\${APPNAME} \${VERSION} (\${GUI_NAME} client)"
OutFile "$2/\${APPNAME}-\${VERSION}-msys2-\${WIN_ARCH}-\${GUI_ID}-setup.exe"

;Variables

Var STARTMENU_FOLDER
Var DefaultLanguageCode
Var LangName

; Pages

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "$1\doc\freeciv\installer\COPYING.installer"
!insertmacro MUI_PAGE_COMPONENTS
Page custom DefaultLanguage DefaultLanguageLeave
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY

; Start Menu Folder Page Configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "SHCTX"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "\$(^Name)"

!insertmacro MUI_PAGE_STARTMENU "Application" \$STARTMENU_FOLDER
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION RunFreeciv
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages

!insertmacro MUI_LANGUAGE "English"

!insertmacro MUI_LANGUAGE "Arabic"
!insertmacro MUI_LANGUAGE "Bulgarian"
!insertmacro MUI_LANGUAGE "Catalan"
!insertmacro MUI_LANGUAGE "Czech"
!insertmacro MUI_LANGUAGE "Danish"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Greek"
!insertmacro MUI_LANGUAGE "Esperanto"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "SpanishInternational"
!insertmacro MUI_LANGUAGE "Estonian"
!insertmacro MUI_LANGUAGE "Farsi"
!insertmacro MUI_LANGUAGE "Finnish"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "Irish"
!insertmacro MUI_LANGUAGE "Hebrew"
!insertmacro MUI_LANGUAGE "Hungarian"
!insertmacro MUI_LANGUAGE "Indonesian"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "Lithuanian"
!insertmacro MUI_LANGUAGE "Dutch"
!insertmacro MUI_LANGUAGE "Norwegian"
!insertmacro MUI_LANGUAGE "Polish"
!insertmacro MUI_LANGUAGE "PortugueseBR"
!insertmacro MUI_LANGUAGE "Portuguese"
!insertmacro MUI_LANGUAGE "Romanian"
!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "Serbian"
!insertmacro MUI_LANGUAGE "Swedish"
!insertmacro MUI_LANGUAGE "Turkish"
!insertmacro MUI_LANGUAGE "Ukrainian"

EOF

### Required files ###

cat <<EOF
; The stuff to install
Section "\${APPNAME} (required)"

  SectionIn RO

  SetOutPath \$INSTDIR
EOF

  # find files and directories to exclude from default installation

  echo -n "  File /nonfatal /r "

  # languages
  echo -n "/x locale "

  # soundsets
  find $1/data -mindepth 1 -maxdepth 1 -name *.soundspec -printf %f\\n |
  sed 's|.soundspec||' |
  while read -r name
  do
  echo -n "/x $name.soundspec /x $name "
  done

  # CJK fonts
  echo -n "/x COPYING.fireflysung "
  echo -n "/x fireflysung.ttf "
  echo -n "/x COPYING.sazanami "
  echo -n "/x sazanami-gothic.ttf "
  echo -n "/x COPYING.UnDotum "
  echo -n "/x UnDotum.ttf "

  echo "$1\\*.*"

cat <<EOF

  ; Write the installation path into the registry
  WriteRegStr "SHCTX" SOFTWARE\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART} "" "\$INSTDIR"

  !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
  CreateDirectory "\$SMPROGRAMS\\\$STARTMENU_FOLDER"
  CreateShortCut "\$SMPROGRAMS\\\$STARTMENU_FOLDER\Freeciv Server.lnk" "\$INSTDIR\freeciv-server.cmd" "\$DefaultLanguageCode" "\$INSTDIR\freeciv-server.exe" 0 SW_SHOWMINIMIZED
  CreateShortCut "\$SMPROGRAMS\\\$STARTMENU_FOLDER\Freeciv Modpack Installer.lnk" "\$INSTDIR\freeciv-mp-gtk4.cmd" "\$DefaultLanguageCode" "\$INSTDIR\freeciv-mp-gtk4.exe" 0 SW_SHOWMINIMIZED
  CreateShortCut "\$SMPROGRAMS\\\$STARTMENU_FOLDER\Freeciv.lnk" "\$INSTDIR\freeciv-sdl2.cmd" "\$DefaultLanguageCode" "\$INSTDIR\freeciv-sdl2.exe" 0 SW_SHOWMINIMIZED
  CreateShortCut "\$SMPROGRAMS\\\$STARTMENU_FOLDER\Documentation.lnk" "\$INSTDIR\doc\freeciv"
  CreateShortCut "\$SMPROGRAMS\\\$STARTMENU_FOLDER\Uninstall.lnk" "\$INSTDIR\uninstall.exe"
  CreateShortCut "\$SMPROGRAMS\\\$STARTMENU_FOLDER\Website.lnk" "\$INSTDIR\Freeciv.url"
  !insertmacro MUI_STARTMENU_WRITE_END

  ; Write the uninstall keys for Windows
  WriteRegStr "SHCTX" "Software\Microsoft\Windows\CurrentVersion\Uninstall\\\${APPID}" "DisplayName" "\$(^Name)"
  WriteRegStr "SHCTX" "Software\Microsoft\Windows\CurrentVersion\Uninstall\\\${APPID}" "UninstallString" '"\$INSTDIR\uninstall.exe"'
  WriteRegDWORD "SHCTX" "Software\Microsoft\Windows\CurrentVersion\Uninstall\\\${APPID}" "NoModify" 1
  WriteRegDWORD "SHCTX" "Software\Microsoft\Windows\CurrentVersion\Uninstall\\\${APPID}" "NoRepair" 1
  WriteUninstaller "uninstall.exe"

  SetOutPath \$INSTDIR
SectionEnd

EOF

### soundsets ###

cat <<EOF
SectionGroup "Soundsets"

EOF

find $1/data -mindepth 1 -maxdepth 1 -name *.soundspec -printf %f\\n |
sort |
sed 's|.soundspec||' |
while read -r name
do
if test -d $1/data/$name; then
echo "  Section \"$name\""
echo "  SetOutPath \$INSTDIR\\data"
echo "  File /r $1\data\\$name.soundspec"
echo "  SetOutPath \$INSTDIR\\data\\$name"
echo "  File /r $1\\data\\$name\*.*"
echo "  SetOutPath \$INSTDIR"
echo "  SectionEnd"
echo
fi
done

cat <<EOF
SectionGroupEnd

EOF

### Additional languages ###

cat <<EOF
SectionGroup "Additional languages (translation %)"

EOF

cat $1/doc/freeciv/installer/langstat_core.txt |
sort -k 1 |
iconv -f UTF-8 -t ISO-8859-1 |
while read -r code prct name
do
if test -e $1/share/locale/$code/LC_MESSAGES/freeciv-core.mo; then
echo "  Section \"$name ($code) $prct\""
echo "  SetOutPath \$INSTDIR\\share\\locale\\$code"
echo "  File /r $1\\share\\locale\\$code\*.*"

# install special fonts for CJK locales
if [ "$name" = "zh_CN" ]; then
echo "  SetOutPath \$INSTDIR\\data\\themes\\gui-sdl2\\human"
echo "  File /r $1\\data\\themes\\gui-sdl2\\human\\COPYING.fireflysung"
echo "  File /r $1\\data\\themes\\gui-sdl2\\human\\fireflysung.ttf"
fi
if [ "$name" = "ja" ]; then
echo "  SetOutPath \$INSTDIR\\data\\themes\\gui-sdl2\\human"
echo "  File /r $1\\data\\themes\\gui-sdl2\\human\\COPYING.sazanami"
echo "  File /r $1\\data\\themes\\gui-sdl2\\human\\sazanami-gothic.ttf"
fi
if [ "$name" = "ko" ]; then
echo "  SetOutPath \$INSTDIR\\data\\themes\\gui-sdl2\\human"
echo "  File /r $1\\data\\themes\\gui-sdl2\\human\\COPYING.UnDotum"
echo "  File /r $1\\data\\themes\\gui-sdl2\\human\\UnDotum.ttf"
fi

echo "  SetOutPath \$INSTDIR"
echo "  SectionEnd"
echo
fi
done

cat <<EOF
SectionGroupEnd

EOF

cat <<EOF
;--------------------------------
; Installer Functions

Function .onInit

  !insertmacro MULTIUSER_INIT

FunctionEnd

Var DefaultLanguageDialog
Var DefaultLanguageLabel
Var DefaultLanguageDropList

Function DefaultLanguage
  !insertmacro MUI_HEADER_TEXT "Choose Default Language" ""

  nsDialogs::Create 1018
  Pop \$DefaultLanguageDialog

  \${If} \$DefaultLanguageDialog == error
    Abort
  \${EndIf}

  \${NSD_CreateLabel} 0 0 100% 30% \
"If you want to play Freeciv in a language other than your Windows language or \
if Freeciv's auto-detection of your Windows language does not work correctly, \
you can select a specific language to be used by Freeciv here. Be sure \
you haven't unmarked the installation of the corresponding language files \
in the previous dialog. You can also change this setting later in the Freeciv \
Start Menu shortcut properties."
  Pop \$DefaultLanguageLabel

  \${NSD_CreateDropList} 0 -60% 100% 13u ""
  Pop \$DefaultLanguageDropList

  \${NSD_CB_AddString} \$DefaultLanguageDropList "Autodetected"
  \${NSD_CB_SelectString} \$DefaultLanguageDropList "Autodetected"
  \${NSD_CB_AddString} \$DefaultLanguageDropList "US English (en_US)"
EOF

  cat $1/doc/freeciv/installer/langstat_core.txt |
  sort -k 1 |
  iconv -f UTF-8 -t ISO-8859-1 |
  while read -r code prct name
  do
  if test -e $1/share/locale/$code/LC_MESSAGES/freeciv-core.mo; then
  echo "  \${NSD_CB_AddString} \$DefaultLanguageDropList \"$name ($code) $prct\""
  fi
  done

cat <<EOF
  nsDialogs::Show
FunctionEnd

Function DefaultLanguageLeave
  \${NSD_GetText} \$DefaultLanguageDropList \$LangName
EOF

  echo "  \${If} \$LangName == \"Autodetected\""
  echo "    StrCpy \$DefaultLanguageCode \"auto\""
  echo "  \${EndIf}"
  echo "  \${If} \$LangName == \"US English (en_US)\""
  echo "    StrCpy \$DefaultLanguageCode \"en_US\""
  echo "  \${EndIf}"

  cat $1/doc/freeciv/installer/langstat_core.txt |
  iconv -f UTF-8 -t ISO-8859-1 |
  while read -r code prct name
  do
    echo "  \${If} \$LangName == \"$name ($code) $prct\""
    echo "    StrCpy \$DefaultLanguageCode \"$code\""
    echo "  \${EndIf}"
  done

cat <<EOF
FunctionEnd

Function RunFreeciv
  nsExec::Exec '"\$INSTDIR\freeciv-sdl2.cmd" \$DefaultLanguageCode'
FunctionEnd

EOF

### Uninstall section ###

cat <<EOF
; Special uninstall section.
Section "Uninstall"

  ; Remove files
EOF

find $1 -type f |
grep -v '/$' |
sed 's|meson/install/||' |
sed 's|autotools/install/||' |
sed 's|[^/]*||' |
tr '/' '\\' | while read -r name
do
echo "  Delete \"\$INSTDIR$name\""
done

find $1 -depth -type d |
grep -v '/$' |
sed 's|meson/install/||' |
sed 's|autotools/install/||' |
sed 's|[^/]*||' |
tr '/' '\\' | while read -r name
do
echo "  RMDir \"\$INSTDIR$name\""
done

cat <<EOF

  ; MUST REMOVE UNINSTALLER, too
  Delete "\$INSTDIR\uninstall.exe"

  ; Remove install directory, if empty
  RMDir "\$INSTDIR"

  ; Remove shortcuts, if any.
  !insertmacro MUI_STARTMENU_GETFOLDER "Application" \$STARTMENU_FOLDER
  Delete "\$SMPROGRAMS\\\$STARTMENU_FOLDER\*.*"
  RMDir "\$SMPROGRAMS\\\$STARTMENU_FOLDER"

  ; Remove registry keys
  DeleteRegKey "SHCTX" "Software\Microsoft\Windows\CurrentVersion\Uninstall\\\${APPID}"
  DeleteRegValue "SHCTX" SOFTWARE\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART} ""
  DeleteRegValue "SHCTX" SOFTWARE\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART} "Start Menu Folder"
  DeleteRegKey /ifempty "SHCTX" SOFTWARE\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}\\\${APP_KEY_PART}
  DeleteRegKey /ifempty "SHCTX" SOFTWARE\\\${KEYROOT}\\\${VERSION}\\\${ARCH_KEY_PART}
  DeleteRegKey /ifempty "SHCTX" SOFTWARE\\\${KEYROOT}\\\${VERSION}
  DeleteRegKey /ifempty "SHCTX" SOFTWARE\\\${KEYROOT}
SectionEnd
EOF

cat <<EOF
;--------------------------------
; Uninstaller Functions

Function un.onInit

  !insertmacro MULTIUSER_UNINIT

FunctionEnd

EOF
