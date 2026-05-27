@ECHO OFF
REM Minimal Sphinx build helper for Windows.
REM
REM Usage:
REM     make.bat html      Build the HTML docs into _build\html
REM     make.bat api       Regenerate classes\ from addon GDScript via godot --doctool
REM     make.bat live      Live-reload server on http://127.0.0.1:8000
REM     make.bat linkcheck Verify external links
REM     make.bat clean     Remove the _build directory

pushd %~dp0

if "%SPHINXBUILD%" == "" set SPHINXBUILD=sphinx-build
set SOURCEDIR=.
set BUILDDIR=_build

if "%1" == "" goto html
if "%1" == "html" goto html
if "%1" == "api" goto api
if "%1" == "live" goto live
if "%1" == "linkcheck" goto linkcheck
if "%1" == "clean" goto clean

echo Unknown target "%1".
echo Valid targets: html, api, live, linkcheck, clean.
exit /b 1

:html
%SPHINXBUILD% -b html %SOURCEDIR% %BUILDDIR%\html %SPHINXOPTS%
if errorlevel 1 exit /b %errorlevel%
echo.
echo Build finished. Open %BUILDDIR%\html\index.html in a browser.
goto end

:api
where godot >NUL 2>NUL
if errorlevel 1 (
    echo godot is not in your PATH. Install Godot 4.6+ and add it to PATH,
    echo or see .github\actions\build-docs-classes\action.yml for the CI flow.
    exit /b 1
)
if not exist api mkdir api
echo [1/3] Extracting class XML via godot --doctool...
godot --doctool "%CD%\api" --gdscript-docs . --headless --path .. --quit
if errorlevel 1 exit /b %errorlevel%
echo [2/3] Filtering private members...
python tools\filter_private.py api api_filtered
if errorlevel 1 exit /b %errorlevel%
echo [3/3] Generating RST into classes\...
python tools\make_rst.py api_filtered --output classes
if errorlevel 1 exit /b %errorlevel%
echo.
echo Class reference updated. Run .\make.bat html next.
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
