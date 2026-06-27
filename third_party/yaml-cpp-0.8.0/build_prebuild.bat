@set SCRIPT_PATH=%~dp0
@pushd %SCRIPT_PATH%\..\..

@echo Remember to uncomment the targets!

@set YAML_PATH=third_party/yaml-cpp-0.8.0

del /F/Q third_party\yaml-cpp-0.8.0\bin\yaml-cpp.lib
bazel build %YAML_PATH%:yaml-cpp
cp bazel-bin\%YAML_PATH%\yaml-cpp.lib %YAML_PATH%\bin\yaml-cpp.lib

@popd
