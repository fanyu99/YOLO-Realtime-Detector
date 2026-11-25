# -----------------------------------------------------------------------------
# 运行时 DLL 部署（Windows）
#
# MSVC 的导入表只记录 DLL 文件名，而依赖所在的 bin 目录通常不在 PATH 上，
# 程序启动时会直接加载失败。部署按 target 进行：主程序与测试程序输出到同一
# 目录，谁先构建谁负责把 DLL 放过去（copy_if_different 保证重复拷贝无害）。
#
# 依赖目录一律从 imported target 的 IMPORTED_LOCATION* 或环境变量 / Cache
# 变量推导，不在本文件里写死任何本机路径。
# -----------------------------------------------------------------------------

option(DEPLOY_CUDA_RUNTIME "Copy CUDA/cuDNN runtime DLLs next to the executable" ON)
option(DEPLOY_MVS_RUNTIME "Copy Hikvision MVS runtime DLLs next to the executable" ON)
set(MVS_RUNTIME_DIR "" CACHE PATH "海康 MVS 运行时目录(留空则自动探测)")

# OpenCV + ONNX Runtime：从 imported target 反推 bin 目录，把目录下的 DLL 一次性
# 全部带走（onnxruntime_providers_*.dll 等也在这里）。用 glob 而不是逐个列文件：
# 以后新增依赖无需改这里。
function(yolo_deploy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()

    set(_dep_dirs "")
    foreach(_dep_target opencv_world onnxruntime)
        if(TARGET ${_dep_target})
            foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
                get_target_property(_loc ${_dep_target} "IMPORTED_LOCATION${_cfg}")
                if(_loc AND EXISTS "${_loc}")
                    get_filename_component(_dir "${_loc}" DIRECTORY)
                    list(APPEND _dep_dirs "${_dir}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _dep_dirs)

    set(_dlls "")
    foreach(_dir IN LISTS _dep_dirs)
        file(GLOB _found "${_dir}/*.dll")
        list(APPEND _dlls ${_found})
    endforeach()

    if(_dlls)
        list(LENGTH _dlls _count)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_dlls}
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Deploying ${_count} runtime DLLs next to ${target}.exe"
        )
    endif()
endfunction()

# CUDA / cuDNN 运行时 DLL。
# onnxruntime_providers_cuda.dll 静态依赖 cublasLt64_13.dll / cublas64_13.dll，
# 运行时还要 LoadLibrary cudart64_13.dll / cudnn64_9.dll / nvrtc64_13x_0.dll。
# 这些 DLL 只由 CUDA Toolkit 和 cuDNN 提供，显卡驱动不带。
# 若已通过 PATH 提供，可用 -DDEPLOY_CUDA_RUNTIME=OFF 关闭。
function(yolo_deploy_cuda_runtime target)
    if(NOT WIN32 OR NOT DEPLOY_CUDA_RUNTIME)
        return()
    endif()

    # 注意目录布局：CUDA 12 及更早把运行时 DLL 放在 bin\，
    # CUDA 13 起改到 bin\x64\；cuDNN 9.x 的压缩包是 bin\x64\。两个都搜，兼容新旧版本。
    set(_cuda_bin_dirs "")
    if(DEFINED ENV{CUDA_PATH})
        list(APPEND _cuda_bin_dirs "$ENV{CUDA_PATH}/bin" "$ENV{CUDA_PATH}/bin/x64")
    endif()
    if(CUDNN_PATH)
        list(APPEND _cuda_bin_dirs "${CUDNN_PATH}/bin" "${CUDNN_PATH}/bin/x64")
    elseif(DEFINED ENV{CUDNN_PATH})
        list(APPEND _cuda_bin_dirs "$ENV{CUDNN_PATH}/bin" "$ENV{CUDNN_PATH}/bin/x64")
    endif()
    file(GLOB _cuda_toolkit_bins
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*/bin"
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*/bin/x64")
    list(APPEND _cuda_bin_dirs ${_cuda_toolkit_bins})
    list(REMOVE_DUPLICATES _cuda_bin_dirs)

    set(_cuda_dlls "")
    foreach(_dir IN LISTS _cuda_bin_dirs)
        foreach(_pat
            "cublas64_13.dll"
            "cublasLt64_13.dll"
            "cudart64_13.dll"
            "nvrtc64_13*_0.dll"
            "nvrtc-builtins64_13*.dll"
            "nvJitLink*.dll"
            "cudnn*.dll")
            file(GLOB _found "${_dir}/${_pat}")
            list(APPEND _cuda_dlls ${_found})
        endforeach()
    endforeach()
    if(_cuda_dlls)
        list(REMOVE_DUPLICATES _cuda_dlls)
    endif()

    if(_cuda_dlls)
        list(LENGTH _cuda_dlls _cuda_dll_count)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_cuda_dlls}
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Deploying ${_cuda_dll_count} CUDA/cuDNN runtime DLLs next to ${target}.exe"
        )
    else()
        message(STATUS
            "DEPLOY_CUDA_RUNTIME=ON, but no CUDA/cuDNN bin directory was found. "
            "Set CUDA_PATH (and CUDNN_PATH) or add them to PATH, otherwise the CUDA EP will fall back to CPU.")
    endif()
endfunction()

# 海康 MVS 运行时 DLL。
#
# exe 的导入表里记录了 MvCameraControl.dll，它自己又依赖 GenICam 栈
# (GCBase/GenApi/XmlParser/...)和传输层(MvGigEVisionSDK / MvUsb3vTL)——
# 少任何一个都是「双击没反应」级别的启动失败，而不是海康功能不可用。
#
# MVS 安装器会把这些放进 Common Files 并加进 PATH，看起来不需要部署，但：
#   1) 那台机器必须装过 MVS SDK —— 只装运行时或走绿色包就未必；
#   2) MVS 会同时把 Win32_i86 和 Win64_x64 都加进 PATH，而 32 位那个常排在
#      前面。按 PATH 顺序查找是先命中名字、再校验架构的，命中一份错架构的
#      就是加载失败，不会继续往后找。
# 所以这里把正确架构的运行时直接复制到 exe 旁边 —— exe 目录在搜索顺序里
# 优先级最高，从此与 PATH 无关。
#
# 整个目录一起复制(约 110MB，大头是 IPP/SuperRender 这些给 MVS 自带查看器
# 用的库)。不想背这个体积就 -DDEPLOY_MVS_RUNTIME=OFF，自行保证 PATH。
function(yolo_deploy_mvs_runtime target)
    if(NOT WIN32 OR NOT DEPLOY_MVS_RUNTIME)
        return()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_mvs_runtime_arch "Win64_x64")
    else()
        set(_mvs_runtime_arch "Win32_i86")
    endif()

    # 安装位置随版本/安装选项变，按常见顺序找第一个含 MvCameraControl.dll 的目录。
    # 注意不能用 $ENV{CommonProgramFiles(x86)} —— CMake 的 $ENV{} 解析不了带括号的
    # 变量名，会直接报语法错误。MVS 是 32 位安装器，固定装到 Common Files (x86)，
    # 所以这里直接写死标准路径，另外留一个 cache 变量给非标准安装。
    if(NOT MVS_RUNTIME_DIR)
        set(_mvs_runtime_candidates
            "${MVS_SDK_ROOT}/Runtime/${_mvs_runtime_arch}"
            "C:/Program Files (x86)/Common Files/MVS/Runtime/${_mvs_runtime_arch}"
            "C:/Program Files/Common Files/MVS/Runtime/${_mvs_runtime_arch}"
        )
        foreach(_dir IN LISTS _mvs_runtime_candidates)
            if(NOT MVS_RUNTIME_DIR AND EXISTS "${_dir}/MvCameraControl.dll")
                set(MVS_RUNTIME_DIR "${_dir}")
            endif()
        endforeach()
    endif()

    if(MVS_RUNTIME_DIR)
        file(GLOB _mvs_files "${MVS_RUNTIME_DIR}/*.dll" "${MVS_RUNTIME_DIR}/*.ini")
        list(LENGTH _mvs_files _mvs_count)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mvs_files}
                $<TARGET_FILE_DIR:${target}>
            COMMENT "Deploying ${_mvs_count} MVS runtime files next to ${target}.exe"
        )
        message(STATUS "MVS 运行时目录: ${MVS_RUNTIME_DIR}")
    else()
        message(STATUS
            "DEPLOY_MVS_RUNTIME=ON, but no MVS runtime directory was found. "
            "MvCameraControl.dll will have to come from PATH; unused if you don't use Hik cameras.")
    endif()
endfunction()
