@set SCRIPT_PATH=%~dp0
@pushd %SCRIPT_PATH%\..\..

@echo Remember to uncomment the targets!

@set YAML_PATH=third_party/Catch2-3.8.0

del /F/Q third_party\Catch2-3.8.0\bin\catch2_main.lib
bazel build %YAML_PATH%:catch2_main
cp bazel-bin\%YAML_PATH%\catch2_main.lib %YAML_PATH%\bin\catch2_main.lib

@popd
