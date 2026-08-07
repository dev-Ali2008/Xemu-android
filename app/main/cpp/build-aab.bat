@echo off
setlocal

set "ROOT=%~dp0"
set "ANDROID_DIR=%ROOT%android"
set "KEY_PROPS=%ANDROID_DIR%\key.properties"

if not exist "%KEY_PROPS%" (
  echo Creating "%KEY_PROPS%" with placeholders...
  >"%KEY_PROPS%" (
    echo storeFile=C:/path/to/your-release-key.jks
    echo storePassword=REPLACE_ME
    echo keyAlias=REPLACE_ME
    echo keyPassword=REPLACE_ME
  )
  echo.
  echo Edit %KEY_PROPS% with your real values, then rerun this script.
  echo.
  pause
  exit /b 1
)

findstr /C:"REPLACE_ME" "%KEY_PROPS%" >nul
if %errorlevel%==0 (
  echo.
  echo Please replace the placeholders in %KEY_PROPS% before building.
  echo.
  pause
  exit /b 1
)

findstr /C:"C:/path/to/your-release-key.jks" "%KEY_PROPS%" >nul
if %errorlevel%==0 (
  echo.
  echo Please replace the storeFile placeholder in %KEY_PROPS% before building.
  echo.
  pause
  exit /b 1
)

pushd "%ANDROID_DIR%" || exit /b 1
call gradlew.bat bundleRelease
set "EXITCODE=%ERRORLEVEL%"
popd

if not "%EXITCODE%"=="0" (
  echo.
  echo Build failed with exit code %EXITCODE%.
  echo.
  pause
  exit /b %EXITCODE%
)

echo.
echo Build complete.
echo Release AAB:
echo   %ANDROID_DIR%\app\build\outputs\bundle\release\app-release.aab
echo.
pause
