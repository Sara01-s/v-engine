@echo off
setlocal enabledelayedexpansion

:: Define directories
set "search_dir=..\engine\shaders"
set "output_dir=..\engine\shaders\compiled"

:: Check if glslc is available
where glslc >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO]: Error: 'glslc' not found. Please ensure it is installed and added to your PATH.
    exit /b 1
) else (
    echo [INFO]: 'glslc' found successfully.
)

:: Check if the output directory exists, if not, create it
if not exist "%output_dir%" (
    echo [INFO]: Output directory does not exist. Creating %output_dir%...
    mkdir "%output_dir%"
    if %errorlevel% equ 0 (
        echo [INFO]: Successfully created %output_dir%.
    ) else (
        echo [INFO]: Failed to create %output_dir%.
        exit /b 1
    )
) else (
    echo [INFO]: Output directory %output_dir% successfully found.
)

:: Counters for shaders
set frag_count=0
set vert_count=0

:: Search and log .frag shaders
echo [INFO]: Searching for shaders