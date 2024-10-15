@echo off
setlocal

:: Path to glslangValidator (make sure it's in your PATH or adjust this line)
set GLSLANG_VALIDATOR=glslangValidator.exe

:: Input folder with shaders
set SHADER_FOLDER=.\
:: Output folder for compiled SPIR-V
set OUTPUT_FOLDER=.\compiled

:: Create output directory if it doesn't exist
if not exist %OUTPUT_FOLDER% (
    mkdir %OUTPUT_FOLDER%
)

:: Compile all vertex shaders (.comp)
for %%f in (%SHADER_FOLDER%\*.comp) do (
    echo Compiling vertex shader %%~nxf...
    %GLSLANG_VALIDATOR% -V %%f -o %OUTPUT_FOLDER%\%%~nf.comp.spv
)

:: Compile all vertex shaders (.vert)
for %%f in (%SHADER_FOLDER%\*.vert) do (
    echo Compiling vertex shader %%~nxf...
    %GLSLANG_VALIDATOR% -V %%f -o %OUTPUT_FOLDER%\%%~nf.vert.spv
)

:: Compile all fragment shaders (.frag)
for %%f in (%SHADER_FOLDER%\*.frag) do (
    echo Compiling fragment shader %%~nxf...
    %GLSLANG_VALIDATOR% -V %%f -o %OUTPUT_FOLDER%\%%~nf.frag.spv
)

echo All shaders compiled!

pause