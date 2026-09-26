
rem %1 should be an already extracted msys2 directory.

rem First just launch the shell for automatic setup
rem of the environment
%1\usr\bin\bash.exe -lc ' ' || exit /b

rem Update base environment
%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Syu' || exit /b
%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Syu' || exit /b

rem Install packages
%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
make ^
tar ^
git ^
patch ^
gdb ^
' || exit /b

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-ucrt-x86_64-pkgconf ^
mingw-w64-ucrt-x86_64-nsis ^
mingw-w64-ucrt-x86_64-gcc ^
mingw-w64-ucrt-x86_64-7zip ^
mingw-w64-ucrt-x86_64-curl ^
mingw-w64-ucrt-x86_64-icu ^
mingw-w64-ucrt-x86_64-sqlite3 ^
mingw-w64-ucrt-x86_64-gtk3 ^
mingw-w64-ucrt-x86_64-gtk4 ^
' || exit /b

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-ucrt-x86_64-SDL2_mixer ^
mingw-w64-ucrt-x86_64-SDL2_image ^
mingw-w64-ucrt-x86_64-SDL2_ttf ^
mingw-w64-ucrt-x86_64-imagemagick ^
mingw-w64-ucrt-x86_64-drmingw ^
mingw-w64-ucrt-x86_64-qt6-base ^
' || exit /b
rem mingw-w64-ucrt-x86_64-SDL2_gfx

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-ucrt-x86_64-meson ^
mingw-w64-ucrt-x86_64-tolua ^
mingw-w64-ucrt-x86_64-qt6-svg ^
mingw-w64-ucrt-x86_64-wget2 ^
mingw-w64-ucrt-x86_64-sdl3-image ^
mingw-w64-ucrt-x86_64-sdl3-ttf ^
mingw-w64-ucrt-x86_64-sdl3-mixer ^
' || exit /b

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-x86_64-pkgconf ^
mingw-w64-x86_64-nsis ^
mingw-w64-x86_64-gcc ^
mingw-w64-x86_64-7zip ^
mingw-w64-x86_64-curl ^
mingw-w64-x86_64-icu ^
mingw-w64-x86_64-sqlite3 ^
mingw-w64-x86_64-gtk3 ^
mingw-w64-x86_64-gtk4 ^
' || exit /b

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-x86_64-qt5-base ^
mingw-w64-x86_64-SDL2_mixer ^
mingw-w64-x86_64-SDL2_image ^
mingw-w64-x86_64-SDL2_ttf ^
mingw-w64-x86_64-imagemagick ^
mingw-w64-x86_64-drmingw ^
' || exit /b
rem mingw-w64-x86_64-SDL2_gfx

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-x86_64-meson ^
mingw-w64-x86_64-tolua ^
mingw-w64-x86_64-wget2 ^
mingw-w64-x86_64-sdl3-image ^
mingw-w64-x86_64-sdl3-ttf ^
mingw-w64-x86_64-sdl3-mixer ^
' || exit /b

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-clang-x86_64-pkgconf ^
mingw-w64-clang-x86_64-nsis ^
mingw-w64-clang-x86_64-gcc ^
mingw-w64-clang-x86_64-curl ^
mingw-w64-clang-x86_64-7zip ^
mingw-w64-clang-x86_64-icu ^
mingw-w64-clang-x86_64-sqlite3 ^
mingw-w64-clang-x86_64-gtk3 ^
mingw-w64-clang-x86_64-gtk4 ^
' || exit /b

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-clang-x86_64-SDL2_mixer ^
mingw-w64-clang-x86_64-SDL2_image ^
mingw-w64-clang-x86_64-SDL2_ttf ^
mingw-w64-clang-x86_64-imagemagick ^
mingw-w64-clang-x86_64-drmingw ^
' || exit /b
rem mingw-w64-clang-x86_64-SDL2_gfx

%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Su --needed ^
mingw-w64-clang-x86_64-qt6-base ^
mingw-w64-clang-x86_64-meson ^
mingw-w64-clang-x86_64-tolua ^
mingw-w64-clang-x86_64-qt6-svg ^
mingw-w64-clang-x86_64-wget2 ^
mingw-w64-clang-x86_64-sdl3-image ^
mingw-w64-clang-x86_64-sdl3-ttf ^
mingw-w64-clang-x86_64-sdl3-mixer ^
' || exit /b

rem Update just in case, so everything is certainly
rem at the same level even if upstream made updates
rem while we were installing.
%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Syu' || exit /b

rem Clear caches
%1\usr\bin\bash.exe -lc 'pacman --noconfirm -Scc' || exit /b
