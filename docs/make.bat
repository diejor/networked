@ECHO OFF
REM Minimal Sphinx build helper for Windows.
REM
REM Usage:
REM     make.bat html      Build the HTML docs into _build\html
REM     make.bat live      Live-reload server on http://127.0.0.1:8000
REM     make.bat linkcheck Verify external links
REM     make.bat clean     Remove the _build directory

pushd %~dp0

if "%SPHINXBUILD%" == "" set SPHINXBUILD=sphinx-build
set SOURCEDIR=.
set BUILDDIR=_build

if "%1" == "" goto html
if "%1" == "html" goto html
if "%1" == "live" goto live
if "%1" == "linkcheck" goto linkcheck
if "%1" == "clean" goto clean

echo Unknown target "%1".
echo Valid targets: html, live, linkcheck, clean.
exit /b 1

:html
%SPHINXBUILD% -b html %SOURCEDIR% %BUILDDIR%\html %SPHINXOPTS%
if errorlevel 1 exit /b %errorlevel%
echo.
echo Build finished. Open %BUILDDIR%\html\index.html in a browser.
goto end

:live
where sphinx-autobuild >NUL 2>NUL
if errorlevel 1 (
    echo sphinx-autobuild is not installed. Install with:
    echo     pip install sphinx-autobuild
    exit /b 1
)
sphinx-autobuild -b html %SOURCEDIR% %BUILDDIR%\html %SPHINXOPTS%
goto end

:linkcheck
%SPHINXBUILD% -b linkcheck %SOURCEDIR% %BUILDDIR%\linkcheck %SPHINXOPTS%
goto end

:clean
if exist %BUILDDIR% rmdir /s /q %BUILDDIR%
echo Removed %BUILDDIR%.
goto end

:end
popd
