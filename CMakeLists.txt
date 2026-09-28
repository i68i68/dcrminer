cmake_minimum_required(VERSION 3.20)

option(DCR_ENABLE_CUDA "Enable CUDA GPU mining support" ON)

if(DCR_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        if(EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin/nvcc.exe")
            set(CMAKE_CUDA_COMPILER "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/bin/nvcc.exe")
        endif()
    endif()
    if(CMAKE_CUDA_COMPILER)
        project(dcrminer VERSION 0.1.0 LANGUAGES CXX CUDA)
    else()
        message(STATUS "CUDA compiler not found. Building CPU/OpenCL backends.")
        set(DCR_ENABLE_CUDA OFF)
        project(dcrminer VERSION 0.1.0 LANGUAGES CXX)
    endif()
else()
    project(dcrminer VERSION 0.1.0 LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Device Backends
option(DCR_ENABLE_HIP "Enable AMD HIP support" OFF)
option(DCR_ENABLE_OPENCL "Enable OpenCL support" ON)

# Universal base compiler flags (safe for all CPUs)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -Wall -Wextra")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /W3")
endif()

# Find Threads
find_package(Threads REQUIRED)

# Common sources
set(COMMON_SOURCES
    src/core/miner.cpp
    src/core/devfee.cpp
    src/tuning/autotuner.cpp
    src/decred/rpc_client.cpp
    src/decred/stratum_client.cpp
    src/blake3/dcr_blake3_avx2.cpp
    src/blake3/dcr_blake3_avx512.cpp
)

# Apply dedicated architecture flags to vector kernels for dynamic CPU dispatch
if(MSVC)
    set_source_files_properties(src/blake3/dcr_blake3_avx2.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX2")
    set_source_files_properties(src/blake3/dcr_blake3_avx512.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX512")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/blake3/dcr_blake3_avx2.cpp PROPERTIES COMPILE_FLAGS "-mavx2")
    set_source_files_properties(src/blake3/dcr_blake3_avx512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl")
endif()

set(EXTRA_LIBS Threads::Threads)
if(WIN32)
    list(APPEND EXTRA_LIBS ws2_32 crypt32 winhttp)
else()
    list(APPEND EXTRA_LIBS dl)
endif()

# CUDA support
if(DCR_ENABLE_CUDA)
    message(STATUS "CUDA Support Enabled: compiling CUDA kernels")
    add_compile_definitions(DCR_ENABLE_CUDA=1)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    
    # Target all modern architectures: Maxwell through Blackwell / Hopper with PTX forward-compatibility
    set(CMAKE_CUDA_ARCHITECTURES 50 52 60 61 70 75 80 86 89 90 compute_90)

    find_package(CUDAToolkit REQUIRED)
    list(APPEND EXTRA_LIBS CUDA::cudart)

    list(APPEND COMMON_SOURCES
        src/gpu/cuda/dcr_blake3.cu
        src/gpu/cuda/cuda_backend.cpp
    )
endif()

if(DCR_ENABLE_HIP)
    add_compile_definitions(DCR_ENABLE_HIP=1)
    list(APPEND COMMON_SOURCES
        src/gpu/hip/dcr_blake3.hip.cpp
        src/gpu/hip/hip_backend.cpp
    )
endif()

if(DCR_ENABLE_OPENCL)
    find_package(OpenCL REQUIRED)
    add_compile_definitions(DCR_ENABLE_OPENCL=1)
    list(APPEND COMMON_SOURCES
        src/gpu/opencl/opencl_backend.cpp
    )
    list(APPEND EXTRA_LIBS OpenCL::OpenCL)
endif()

# Main executable
add_executable(dcrminer src/main.cpp ${COMMON_SOURCES})
target_include_directories(dcrminer PRIVATE src)
target_link_libraries(dcrminer PRIVATE ${EXTRA_LIBS})

# Benchmark executable
add_executable(dcr_benchmark tools/benchmark.cpp ${COMMON_SOURCES})
target_include_directories(dcr_benchmark PRIVATE src)
target_link_libraries(dcr_benchmark PRIVATE ${EXTRA_LIBS})

