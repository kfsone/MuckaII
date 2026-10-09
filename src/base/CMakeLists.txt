# Header-only: the shared type aliases and wire byte constants (docs/spec/architecture.md "Layers").
add_library(muckaiiBase INTERFACE)
target_include_directories(muckaiiBase INTERFACE include)

muckaiiTests(base SOURCES baseTests.cpp)
