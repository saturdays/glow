include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(onnxifi-glow
            SHARED
              Base.cpp
              GlowOnnxifiManager.cpp
              onnxifiGlow.cpp)

add_library(onnxifi-glow-lib
              Base.cpp
              GlowOnnxifiManager.cpp
              onnxifiGlow.cpp)

target_link_libraries(onnxifi-glow-lib
                      PUBLIC
                        ExecutionEngine
                        Graph
                        Importer
                        ThreadPool)

target_link_libraries(onnxifi-glow
                      PUBLIC
                        ExecutionEngine
                        Graph
                        Importer
                        ThreadPool)
			
target_compile_definitions(onnxifi-glow
                           PRIVATE
                             ONNXIFI_BUILD_LIBRARY)

target_compile_definitions(onnxifi-glow-lib
                           PRIVATE
                             ONNXIFI_BUILD_LIBRARY)
